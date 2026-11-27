package main

// End-to-end tests: real TLS WebSocket clients and UDP sockets against a running lobby. The matchmaker is
// ticked by hand so each step is deterministic.

import (
	"bufio"
	"crypto/rand"
	"crypto/tls"
	"encoding/base64"
	"encoding/binary"
	"encoding/json"
	"fmt"
	"io"
	"net"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

// ---------------------------------------------------------------- a test client

type tclient struct {
	t    *testing.T
	conn net.Conn
	r    *bufio.Reader
	udp  *net.UDPConn
	name string
	tok  string // account token
	udpT string // reflect token
	port int
}

func dial(t *testing.T, addr string) *tclient {
	t.Helper()
	conn, err := tls.Dial("tcp", addr, &tls.Config{InsecureSkipVerify: true})
	if err != nil {
		t.Fatal(err)
	}
	key := make([]byte, 16)
	rand.Read(key)
	fmt.Fprintf(conn, "GET /v1 HTTP/1.1\r\nHost: x\r\nUpgrade: websocket\r\nConnection: Upgrade\r\nSec-WebSocket-Key: %s\r\nSec-WebSocket-Version: 13\r\n\r\n",
		base64.StdEncoding.EncodeToString(key))
	r := bufio.NewReader(conn)
	resp, err := http.ReadResponse(r, nil)
	if err != nil || resp.StatusCode != 101 {
		t.Fatalf("upgrade: %v %v", err, resp)
	}
	return &tclient{t: t, conn: conn, r: r}
}

func (c *tclient) send(v any) {
	b, _ := json.Marshal(v)
	hdr := []byte{0x81}
	if len(b) < 126 {
		hdr = append(hdr, 0x80|byte(len(b)))
	} else {
		hdr = append(hdr, 0x80|126, byte(len(b)>>8), byte(len(b)))
	}
	mask := []byte{1, 2, 3, 4}
	hdr = append(hdr, mask...)
	for i := range b {
		b[i] ^= mask[i%4]
	}
	c.conn.Write(append(hdr, b...))
}

// next returns the next message of type typ (skipping stats), failing after a timeout.
func (c *tclient) next(typ string) map[string]any {
	c.t.Helper()
	deadline := time.Now().Add(3 * time.Second)
	for {
		c.conn.SetReadDeadline(deadline)
		var h [2]byte
		if _, err := io.ReadFull(c.r, h[:]); err != nil {
			c.t.Fatalf("%s waiting for %q: %v", c.name, typ, err)
		}
		n := int(h[1] & 0x7F)
		if n == 126 {
			var e [2]byte
			io.ReadFull(c.r, e[:])
			n = int(binary.BigEndian.Uint16(e[:]))
		} else if n == 127 {
			var e [8]byte
			io.ReadFull(c.r, e[:])
			n = int(binary.BigEndian.Uint64(e[:]))
		}
		b := make([]byte, n)
		io.ReadFull(c.r, b)
		if h[0]&0x0F != 1 {
			continue
		}
		var m map[string]any
		json.Unmarshal(b, &m)
		if m["t"] == "stats" || m["t"] == "pong" {
			continue
		}
		if m["t"] != typ {
			c.t.Fatalf("%s: wanted %q, got %v", c.name, typ, m)
		}
		return m
	}
}

// expectNothing: no message (other than stats) arrives for a moment.
func (c *tclient) expectNothing() {
	c.t.Helper()
	c.conn.SetReadDeadline(time.Now().Add(300 * time.Millisecond))
	var h [2]byte
	if _, err := io.ReadFull(c.r, h[:]); err == nil {
		c.t.Fatalf("%s: unexpected message", c.name)
	}
	// the read timed out: the connection is still usable for the next deadline
}

func register(t *testing.T, addr, name string) *tclient {
	c := dial(t, addr)
	c.name = name
	c.send(map[string]any{"t": "register", "v": 1, "name": name, "client": "0.5.0-alpha"})
	m := c.next("registered")
	c.tok, c.udpT = m["token"].(string), m["udpToken"].(string)
	c.port = int(m["udpPort"].(float64))
	return c
}

// reflect sends a reflect packet and waits for the answer (so the server knows our public address).
func (c *tclient) reflect() string {
	c.t.Helper()
	if c.udp == nil {
		u, err := net.ListenUDP("udp", &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1)})
		if err != nil {
			c.t.Fatal(err)
		}
		c.udp = u
	}
	tok, _ := parseToken(c.udpT)
	c.udp.WriteToUDP(append([]byte{udpReflect}, tok[:]...), &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: c.port})
	buf := make([]byte, 256)
	c.udp.SetReadDeadline(time.Now().Add(2 * time.Second))
	n, _, err := c.udp.ReadFromUDP(buf)
	if err != nil || buf[0] != udpReflected {
		c.t.Fatalf("reflect: %v", err)
	}
	return string(buf[1:n])
}

func newTestServer(t *testing.T) (*Lobby, string) {
	t.Helper()
	store, err := OpenStore(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	udp, err := NewUDPServer("127.0.0.1:0")
	if err != nil {
		t.Fatal(err)
	}
	go udp.Serve()
	lobby := NewLobby(store, udp)
	srv := httptest.NewUnstartedServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if c, err := wsUpgrade(w, r); err == nil {
			lobby.Serve(c, r.RemoteAddr)
		}
	}))
	srv.StartTLS()
	t.Cleanup(srv.Close)
	return lobby, strings.TrimPrefix(srv.URL, "https://")
}

// ---------------------------------------------------------------- tests

func TestAccounts(t *testing.T) {
	_, addr := newTestServer(t)
	a := register(t, addr, "Mumbai")

	b := dial(t, addr)
	b.name = "dup"
	b.send(map[string]any{"t": "register", "name": "mumbai", "client": "0.5.0-alpha"})
	if m := b.next("error"); m["code"] != "name_taken" {
		t.Fatalf("duplicate name: %v", m)
	}
	c := dial(t, addr)
	c.name = "bad"
	c.send(map[string]any{"t": "register", "name": "a b", "client": "0.5.0-alpha"})
	if m := c.next("error"); m["code"] != "bad_name" {
		t.Fatalf("bad name: %v", m)
	}
	d := dial(t, addr)
	d.name = "old"
	d.send(map[string]any{"t": "hello", "token": a.tok, "client": "0.4.0"})
	if m := d.next("error"); m["code"] != "outdated" {
		t.Fatalf("old client: %v", m)
	}
	e := dial(t, addr)
	e.name = "again"
	e.send(map[string]any{"t": "hello", "token": a.tok, "client": "0.5.0-alpha"})
	if m := e.next("welcome"); m["name"] != "Mumbai" || m["rating"].(float64) != 1500 {
		t.Fatalf("login: %v", m)
	}
	a.next("error") // the first session is told it was replaced
	f := dial(t, addr)
	f.name = "nobody"
	f.send(map[string]any{"t": "hello", "token": strings.Repeat("ab", 32), "client": "0.5.0-alpha"})
	if m := f.next("error"); m["code"] != "bad_token" {
		t.Fatalf("bad token: %v", m)
	}
}

func TestSideLockIsHard(t *testing.T) {
	lobby, addr := newTestServer(t)
	a := register(t, addr, "LeftA")
	b := register(t, addr, "LeftB")
	a.send(map[string]any{"t": "queue", "mode": "ranked", "side": "p1"})
	a.next("queued")
	b.send(map[string]any{"t": "queue", "mode": "ranked", "side": "p1"})
	b.next("queued")
	for i := 0; i < 5; i++ {
		lobby.Tick(time.Now().Add(time.Duration(i) * time.Minute)) // even after long waits
	}
	a.expectNothing()
	b.expectNothing()

	c := register(t, addr, "Flexible")
	c.send(map[string]any{"t": "queue", "mode": "ranked", "side": "any"})
	c.next("queued")
	lobby.Tick(time.Now())
	pa := a.next("proposal") // the oldest 1P-locked player gets the flexible one
	pc := c.next("proposal")
	if pa["side"] != "p1" || pc["side"] != "p2" {
		t.Fatalf("sides: a=%v c=%v", pa["side"], pc["side"])
	}
	b.expectNothing()
}

func TestSideAssignment(t *testing.T) {
	cases := []struct{ a, b, wantP1 string }{
		{"p1", "any", "a"}, {"any", "p1", "b"}, {"p2", "any", "b"}, {"any", "p2", "a"}, {"p1", "p2", "a"}, {"p2", "p1", "b"},
	}
	for _, c := range cases {
		a, b := &QueueEntry{side: c.a}, &QueueEntry{side: c.b}
		p1, _ := assignSides(a, b)
		got := "a"
		if p1 == b {
			got = "b"
		}
		if got != c.wantP1 {
			t.Errorf("%s vs %s: 1P = %s, want %s", c.a, c.b, got, c.wantP1)
		}
	}
	if compatibleSides("p1", "p1") || compatibleSides("p2", "p2") || !compatibleSides("any", "any") || !compatibleSides("p1", "p2") {
		t.Error("compatibleSides")
	}
}

func TestFullRankedMatchWithRelay(t *testing.T) {
	lobby, addr := newTestServer(t)
	a := register(t, addr, "Alpha")
	b := register(t, addr, "Bravo")
	addrA, addrB := a.reflect(), b.reflect()
	a.send(map[string]any{"t": "queue", "mode": "ranked", "side": "p1", "maxPing": 100})
	a.next("queued")
	b.send(map[string]any{"t": "queue", "mode": "ranked", "side": "any", "maxPing": 150})
	b.next("queued")
	lobby.Tick(time.Now())
	pa, pb := a.next("proposal"), b.next("proposal")
	if pa["peer"].(map[string]any)["addr"] != addrB || pb["peer"].(map[string]any)["addr"] != addrA {
		t.Fatalf("peer addresses: %v / %v (want %s / %s)", pa["peer"], pb["peer"], addrB, addrA)
	}
	if pa["opponent"].(map[string]any)["name"] != "Bravo" {
		t.Fatalf("opponent: %v", pa["opponent"])
	}

	// relay: B announces itself, A sends, B receives (and back)
	server := &net.UDPAddr{IP: net.IPv4(127, 0, 0, 1), Port: a.port}
	relayTok := func(p map[string]any) []byte {
		k, _ := parseToken(p["relay"].(map[string]any)["token"].(string))
		return k[:]
	}
	send := func(c *tclient, tok []byte, payload string) {
		pkt := append([]byte{udpRelay}, tok...)
		c.udp.WriteToUDP(append(pkt, payload...), server)
	}
	recv := func(c *tclient) string {
		buf := make([]byte, 2048)
		c.udp.SetReadDeadline(time.Now().Add(2 * time.Second))
		n, _, err := c.udp.ReadFromUDP(buf)
		if err != nil || buf[0] != udpRelayed {
			t.Fatalf("%s relay receive: %v", c.name, err)
		}
		return string(buf[1:n])
	}
	send(b, relayTok(pb), "hello-from-b") // A has no address yet: dropped, but B is now known
	time.Sleep(50 * time.Millisecond)
	send(a, relayTok(pa), "inputs-frame-1")
	if got := recv(b); got != "inputs-frame-1" {
		t.Fatalf("relayed to B: %q", got)
	}
	send(b, relayTok(pb), "inputs-frame-2")
	if got := recv(a); got != "inputs-frame-2" {
		t.Fatalf("relayed to A: %q", got)
	}

	match := pa["match"].(string)
	a.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 40, "direct": true})
	b.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 42, "direct": true})
	ga, gb := a.next("go"), b.next("go")
	if ga["side"] != "p1" || gb["side"] != "p2" || ga["seed"] != gb["seed"] {
		t.Fatalf("go: %v / %v", ga, gb)
	}

	a.send(map[string]any{"t": "result", "match": match, "winner": "me"})
	b.send(map[string]any{"t": "result", "match": match, "winner": "opponent"})
	ra, rb := a.next("rated"), b.next("rated")
	if ra["change"].(float64) != 20 || rb["change"].(float64) != -20 || ra["rating"].(float64) != 1520 || ra["outcome"] != "agreed" {
		t.Fatalf("rated: %v / %v", ra, rb)
	}

	// the relay closes with the match
	send(a, relayTok(pa), "late")
	b.udp.SetReadDeadline(time.Now().Add(300 * time.Millisecond))
	if _, _, err := b.udp.ReadFromUDP(make([]byte, 64)); err == nil {
		t.Fatal("relay still open after the match")
	}
}

func TestTooFarRequeuesAndAvoidsPair(t *testing.T) {
	lobby, addr := newTestServer(t)
	a := register(t, addr, "Near")
	b := register(t, addr, "Far")
	a.send(map[string]any{"t": "queue", "mode": "casual", "side": "any", "maxPing": 80})
	a.next("queued")
	b.send(map[string]any{"t": "queue", "mode": "casual", "side": "any", "maxPing": 200})
	b.next("queued")
	lobby.Tick(time.Now())
	match := a.next("proposal")["match"].(string)
	b.next("proposal")
	a.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 140})
	b.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 150})
	if m := a.next("cancelled"); m["reason"] != "too_far" {
		t.Fatalf("cancel: %v", m)
	}
	a.next("queued")
	b.next("cancelled")
	b.next("queued")
	lobby.Tick(time.Now()) // the same pair is not proposed again right away
	a.expectNothing()
	b.expectNothing()
}

func TestOpponentLeavesDuringProposal(t *testing.T) {
	lobby, addr := newTestServer(t)
	a := register(t, addr, "Stays")
	b := register(t, addr, "Leaves")
	a.send(map[string]any{"t": "queue", "mode": "casual"})
	a.next("queued")
	b.send(map[string]any{"t": "queue", "mode": "casual"})
	b.next("queued")
	lobby.Tick(time.Now())
	a.next("proposal")
	b.next("proposal")
	b.conn.Close()
	if m := a.next("cancelled"); m["reason"] != "opponent_left" {
		t.Fatalf("cancel: %v", m)
	}
	a.next("queued")
}

func TestDisputedResultChangesNothing(t *testing.T) {
	lobby, addr := newTestServer(t)
	a := register(t, addr, "Liar1")
	b := register(t, addr, "Liar2")
	a.send(map[string]any{"t": "queue", "mode": "ranked"})
	a.next("queued")
	b.send(map[string]any{"t": "queue", "mode": "ranked"})
	b.next("queued")
	lobby.Tick(time.Now())
	match := a.next("proposal")["match"].(string)
	b.next("proposal")
	a.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 30})
	b.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 30})
	a.next("go")
	b.next("go")
	a.send(map[string]any{"t": "result", "match": match, "winner": "me"})
	b.send(map[string]any{"t": "result", "match": match, "winner": "me"})
	if m := a.next("rated"); m["outcome"] != "disputed" || m["change"].(float64) != 0 || m["rating"].(float64) != 1500 {
		t.Fatalf("disputed: %v", m)
	}
}

func TestForfeitAfterLeaving(t *testing.T) {
	lobby, addr := newTestServer(t)
	a := register(t, addr, "Winner")
	b := register(t, addr, "Quitter")
	a.send(map[string]any{"t": "queue", "mode": "ranked"})
	a.next("queued")
	b.send(map[string]any{"t": "queue", "mode": "ranked"})
	b.next("queued")
	lobby.Tick(time.Now())
	match := a.next("proposal")["match"].(string)
	b.next("proposal")
	a.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 30})
	b.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 30})
	a.next("go")
	b.next("go")
	b.conn.Close()
	time.Sleep(100 * time.Millisecond)
	a.send(map[string]any{"t": "result", "match": match, "winner": "me"})
	time.Sleep(100 * time.Millisecond)
	lobby.Tick(time.Now()) // too early: nothing yet
	a.expectNothing()
	lobby.Tick(time.Now().Add(forfeitAfter + time.Second))
	if m := a.next("rated"); m["outcome"] != "forfeit" || m["change"].(float64) <= 0 {
		t.Fatalf("forfeit: %v", m)
	}
}

func TestAbandonedWhenTheOneWhoStayedNeverReports(t *testing.T) {
	lobby, addr := newTestServer(t)
	a := register(t, addr, "Winner")
	b := register(t, addr, "Quitter")
	a.send(map[string]any{"t": "queue", "mode": "ranked"})
	a.next("queued")
	b.send(map[string]any{"t": "queue", "mode": "ranked"})
	b.next("queued")
	lobby.Tick(time.Now())
	match := a.next("proposal")["match"].(string)
	b.next("proposal")
	a.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 30})
	b.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 30})
	a.next("go")
	b.next("go")
	b.conn.Close()
	time.Sleep(100 * time.Millisecond)
	lobby.Tick(time.Now().Add(forfeitAfter + time.Second)) // gone, but the one who stayed may still report
	a.expectNothing()
	lobby.Tick(time.Now().Add(forfeitAfter + abandonAfter + time.Second))
	if m := a.next("rated"); m["outcome"] != "abandoned" || m["change"].(float64) != 0 {
		t.Fatalf("abandoned: %v", m)
	}
}

func TestRatingWindowWidens(t *testing.T) {
	if ratingWindow(0) != 100 || ratingWindow(10*time.Second) != 350 || ratingWindow(time.Hour) != 1000 {
		t.Fatal("ratingWindow")
	}
	if g, l := eloChange(1500, 1500, 40, 40); g != 20 || l != -20 {
		t.Fatalf("elo even: %d %d", g, l)
	}
	if g, _ := eloChange(2000, 1000, 24, 24); g != 1 {
		t.Fatalf("elo minimum: %d", g)
	}
}

func TestLeave(t *testing.T) {
	lobby, addr := newTestServer(t)
	a := register(t, addr, "LeaveA")
	b := register(t, addr, "LeaveB")
	a.send(map[string]any{"t": "queue", "mode": "casual"})
	a.next("queued")
	b.send(map[string]any{"t": "queue", "mode": "casual"})
	b.next("queued")
	lobby.Tick(time.Now())
	match := a.next("proposal")["match"].(string)
	b.next("proposal")
	a.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 30})
	b.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 30})
	a.next("go")
	b.next("go")
	a.send(map[string]any{"t": "leave", "match": match})
	b.next("left")
	b.send(map[string]any{"t": "leave", "match": match})
	time.Sleep(100 * time.Millisecond)
	lobby.mu.Lock()
	n := len(lobby.matches)
	lobby.mu.Unlock()
	if n != 0 {
		t.Fatalf("%d matches left open", n)
	}
	// both can queue again
	a.send(map[string]any{"t": "queue", "mode": "casual"})
	a.next("queued")
}

func TestRename(t *testing.T) {
	st, err := OpenStore(t.TempDir())
	if err != nil {
		t.Fatal(err)
	}
	a, _, _ := st.Register("alpha")
	b, _, _ := st.Register("bravo")
	if err := st.Rename(a, "Bravo"); err != errNameTaken {
		t.Fatalf("taken name: %v", err)
	}
	if err := st.Rename(a, "x"); err != errBadName {
		t.Fatalf("bad name: %v", err)
	}
	if err := st.Rename(a, "Alpha2"); err != nil || a.Name != "Alpha2" {
		t.Fatalf("rename: %v %s", err, a.Name)
	}
	if _, _, err := st.Register("alpha"); err != nil {
		t.Fatalf("old name should be free: %v", err)
	}
	if err := st.Rename(b, "BRAVO"); err != nil {
		t.Fatalf("own name, other case: %v", err)
	}
}

// The game's ranks on the server: fights both PCs report alike move them, reports that differ move nothing.
func TestRankedFightsMoveTheGamesRanks(t *testing.T) {
	play := func(lobby *Lobby, addr, n1, n2 string, fa, fb []map[string]any) (map[string]any, map[string]any) {
		a := register(t, addr, n1)
		b := register(t, addr, n2)
		a.send(map[string]any{"t": "queue", "mode": "ranked", "side": "p1"})
		a.next("queued")
		b.send(map[string]any{"t": "queue", "mode": "ranked", "side": "p2"})
		b.next("queued")
		lobby.Tick(time.Now())
		match := a.next("proposal")["match"].(string)
		b.next("proposal")
		a.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 30})
		b.send(map[string]any{"t": "check", "match": match, "ok": true, "rtt": 30})
		a.next("go")
		b.next("go")
		a.send(map[string]any{"t": "result", "match": match, "winner": "me", "fights": fa})
		b.send(map[string]any{"t": "result", "match": match, "winner": "opponent", "fights": fb})
		return a.next("rated"), b.next("rated")
	}
	// Kazuya (14) and Jun (42) beat Paul (0) SOLO twice: beginners win 200 points a fight
	fights := []map[string]any{
		{"teams": [][]int{{14, 42}, {0, 0}}, "winner": 0},
		{"teams": [][]int{{14, 42}, {0, 0}}, "winner": 0},
	}
	lobby, addr := newTestServer(t)
	ra, rb := play(lobby, addr, "Kaz", "Paul", fights, fights)
	ranks := ra["ranks"].([]any)
	if len(ranks) != 2 {
		t.Fatalf("winner's rank changes: %v", ra["ranks"])
	}
	kaz := ranks[0].(map[string]any)
	if kaz["c"].(float64) != 14 || kaz["before"].(float64) != 0 || kaz["after"].(float64) != 2 || kaz["pa"].(float64) != 400 {
		t.Fatalf("Kazuya after two wins from Beginner: %v", kaz)
	}
	if chars := ra["chars"].([]any); len(chars) != numChars || chars[14].(map[string]any)["rank"].(float64) != 2 {
		t.Fatalf("the winner's ranks: %v", ra["chars"])
	}
	if paul := rb["ranks"].([]any); len(paul) != 1 || paul[0].(map[string]any)["c"].(float64) != 0 {
		t.Fatalf("the loser's (SOLO) rank changes: %v", rb["ranks"])
	}
	// the reports' teams differ: the result counts, the ranks do not move
	other := []map[string]any{{"teams": [][]int{{14, 42}, {1, 1}}, "winner": 0}, fights[1]}
	ra, _ = play(lobby, addr, "Kaz2", "Paul2", fights, other)
	if ra["outcome"] != "agreed" || ra["ranks"] != nil || ra["chars"].([]any)[14].(map[string]any)["rank"].(float64) != 0 {
		t.Fatalf("differing fights moved ranks: %v", ra)
	}
}
