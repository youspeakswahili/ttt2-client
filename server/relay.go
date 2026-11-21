package main

// UDP: address reflection (each player learns, and the server records, their public address for hole
// punching) and the relay for matches whose PCs cannot reach each other directly.

import (
	"encoding/hex"
	"net"
	"net/netip"
	"sync"
	"time"
)

const (
	udpReflect   = 0x01
	udpReflected = 0x02
	udpRelay     = 0x03
	udpRelayed   = 0x04
	udpMaxBody   = 1200
)

type relaySide struct {
	addr   netip.AddrPort
	seen   time.Time
	tokens float64 // rate limit bucket (packets)
	last   time.Time
}

type relayPair struct {
	sides   [2]relaySide
	expires time.Time
}

type relayKey struct {
	pair *relayPair
	slot int
}

type UDPServer struct {
	conn *net.UDPConn
	mu   sync.Mutex
	// player tokens -> last public address seen
	players map[[16]byte]*playerAddr
	relays  map[[16]byte]relayKey
}

type playerAddr struct {
	addr netip.AddrPort
	seen time.Time
}

func parseToken(hexToken string) ([16]byte, bool) {
	var t [16]byte
	b, err := hex.DecodeString(hexToken)
	if err != nil || len(b) != 16 {
		return t, false
	}
	copy(t[:], b)
	return t, true
}

func NewUDPServer(addr string) (*UDPServer, error) {
	ua, err := net.ResolveUDPAddr("udp", addr)
	if err != nil {
		return nil, err
	}
	c, err := net.ListenUDP("udp", ua)
	if err != nil {
		return nil, err
	}
	c.SetReadBuffer(4 << 20)
	c.SetWriteBuffer(4 << 20)
	return &UDPServer{conn: c, players: map[[16]byte]*playerAddr{}, relays: map[[16]byte]relayKey{}}, nil
}

func (u *UDPServer) Port() int { return u.conn.LocalAddr().(*net.UDPAddr).Port }

// AddPlayer registers a player's reflect token.
func (u *UDPServer) AddPlayer(token string) {
	t, ok := parseToken(token)
	if !ok {
		return
	}
	u.mu.Lock()
	u.players[t] = &playerAddr{}
	u.mu.Unlock()
}

func (u *UDPServer) RemovePlayer(token string) {
	t, ok := parseToken(token)
	if !ok {
		return
	}
	u.mu.Lock()
	delete(u.players, t)
	u.mu.Unlock()
}

// PlayerAddr is the player's public UDP address as last reflected ("" if unknown or stale).
func (u *UDPServer) PlayerAddr(token string) string {
	t, ok := parseToken(token)
	if !ok {
		return ""
	}
	u.mu.Lock()
	defer u.mu.Unlock()
	p := u.players[t]
	if p == nil || !p.addr.IsValid() || time.Since(p.seen) > 30*time.Second {
		return ""
	}
	return p.addr.String()
}

// NewRelay makes a relay for one match: two tokens (slot 0 and 1), valid for ttl.
func (u *UDPServer) NewRelay(ttl time.Duration) (string, string) {
	pair := &relayPair{expires: time.Now().Add(ttl)}
	t0, t1 := randHex(16), randHex(16)
	k0, _ := parseToken(t0)
	k1, _ := parseToken(t1)
	u.mu.Lock()
	u.relays[k0] = relayKey{pair, 0}
	u.relays[k1] = relayKey{pair, 1}
	u.mu.Unlock()
	return t0, t1
}

func (u *UDPServer) CloseRelay(t0, t1 string) {
	k0, ok0 := parseToken(t0)
	k1, ok1 := parseToken(t1)
	u.mu.Lock()
	if ok0 {
		delete(u.relays, k0)
	}
	if ok1 {
		delete(u.relays, k1)
	}
	u.mu.Unlock()
}

func (u *UDPServer) Serve() {
	buf := make([]byte, 2048)
	go u.sweep()
	for {
		n, from, err := u.conn.ReadFromUDPAddrPort(buf)
		if err != nil {
			if ne, ok := err.(net.Error); ok && ne.Timeout() {
				continue
			}
			return
		}
		if n < 17 {
			continue
		}
		var tok [16]byte
		copy(tok[:], buf[1:17])
		switch buf[0] {
		case udpReflect:
			u.mu.Lock()
			p := u.players[tok]
			if p != nil {
				p.addr, p.seen = from, time.Now()
			}
			u.mu.Unlock()
			if p != nil {
				reply := append([]byte{udpReflected}, []byte(from.String())...)
				u.conn.WriteToUDPAddrPort(reply, from)
			}
		case udpRelay:
			body := buf[17:n]
			if len(body) > udpMaxBody {
				continue
			}
			now := time.Now()
			u.mu.Lock()
			k, ok := u.relays[tok]
			var to netip.AddrPort
			if ok && now.Before(k.pair.expires) {
				me := &k.pair.sides[k.slot]
				// token bucket: 300 packets a second, bursts of 300
				if me.last.IsZero() {
					me.tokens = 300
				} else {
					me.tokens += now.Sub(me.last).Seconds() * 300
					if me.tokens > 300 {
						me.tokens = 300
					}
				}
				me.last = now
				if me.tokens >= 1 {
					me.tokens--
					me.addr, me.seen = from, now
					to = k.pair.sides[1-k.slot].addr
				}
			}
			u.mu.Unlock()
			if to.IsValid() {
				out := make([]byte, 0, 1+len(body))
				out = append(out, udpRelayed)
				out = append(out, body...)
				u.conn.WriteToUDPAddrPort(out, to)
			}
		}
	}
}

// sweep drops expired relays every minute.
func (u *UDPServer) sweep() {
	for range time.Tick(time.Minute) {
		now := time.Now()
		u.mu.Lock()
		for k, v := range u.relays {
			if now.After(v.pair.expires) {
				delete(u.relays, k)
			}
		}
		u.mu.Unlock()
	}
}
