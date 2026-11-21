package main

// A minimal WebSocket server side (RFC 6455): text frames, ping/pong, close. Enough for one JSON
// message per frame; no extensions, no fragmentation beyond reassembling continuation frames.

import (
	"bufio"
	"crypto/sha1"
	"encoding/base64"
	"encoding/binary"
	"errors"
	"io"
	"net"
	"net/http"
	"strings"
	"sync"
	"time"
)

const wsMaxMessage = 64 << 10

type wsConn struct {
	conn net.Conn
	r    *bufio.Reader
	wmu  sync.Mutex
}

var errWsClosed = errors.New("websocket closed")

func headerHas(h http.Header, key, token string) bool {
	for _, v := range h.Values(key) {
		for _, part := range strings.Split(v, ",") {
			if strings.EqualFold(strings.TrimSpace(part), token) {
				return true
			}
		}
	}
	return false
}

// wsUpgrade answers the HTTP upgrade and takes the connection over.
func wsUpgrade(w http.ResponseWriter, r *http.Request) (*wsConn, error) {
	if r.Method != http.MethodGet || !headerHas(r.Header, "Connection", "upgrade") ||
		!headerHas(r.Header, "Upgrade", "websocket") || r.Header.Get("Sec-WebSocket-Version") != "13" {
		http.Error(w, "websocket only", http.StatusBadRequest)
		return nil, errors.New("not a websocket request")
	}
	key := r.Header.Get("Sec-WebSocket-Key")
	if key == "" {
		http.Error(w, "missing key", http.StatusBadRequest)
		return nil, errors.New("missing key")
	}
	h, ok := w.(http.Hijacker)
	if !ok {
		return nil, errors.New("cannot hijack")
	}
	conn, rw, err := h.Hijack()
	if err != nil {
		return nil, err
	}
	sum := sha1.Sum([]byte(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
	resp := "HTTP/1.1 101 Switching Protocols\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n" +
		"Sec-WebSocket-Accept: " + base64.StdEncoding.EncodeToString(sum[:]) + "\r\n\r\n"
	if _, err := conn.Write([]byte(resp)); err != nil {
		conn.Close()
		return nil, err
	}
	return &wsConn{conn: conn, r: rw.Reader}, nil
}

func (c *wsConn) writeFrame(op byte, payload []byte) error {
	c.wmu.Lock()
	defer c.wmu.Unlock()
	hdr := make([]byte, 0, 10)
	hdr = append(hdr, 0x80|op)
	switch n := len(payload); {
	case n < 126:
		hdr = append(hdr, byte(n))
	case n <= 0xFFFF:
		hdr = append(hdr, 126, byte(n>>8), byte(n))
	default:
		hdr = append(hdr, 127)
		hdr = binary.BigEndian.AppendUint64(hdr, uint64(n))
	}
	c.conn.SetWriteDeadline(time.Now().Add(10 * time.Second))
	if _, err := c.conn.Write(append(hdr, payload...)); err != nil {
		return err
	}
	return nil
}

// WriteText sends one text message.
func (c *wsConn) WriteText(b []byte) error { return c.writeFrame(0x1, b) }

// ReadText returns the next text message (answering pings on the way).
func (c *wsConn) ReadText(timeout time.Duration) ([]byte, error) {
	var msg []byte
	for {
		c.conn.SetReadDeadline(time.Now().Add(timeout))
		var h [2]byte
		if _, err := io.ReadFull(c.r, h[:]); err != nil {
			return nil, err
		}
		fin, op := h[0]&0x80 != 0, h[0]&0x0F
		masked := h[1]&0x80 != 0
		n := uint64(h[1] & 0x7F)
		switch n {
		case 126:
			var e [2]byte
			if _, err := io.ReadFull(c.r, e[:]); err != nil {
				return nil, err
			}
			n = uint64(binary.BigEndian.Uint16(e[:]))
		case 127:
			var e [8]byte
			if _, err := io.ReadFull(c.r, e[:]); err != nil {
				return nil, err
			}
			n = binary.BigEndian.Uint64(e[:])
		}
		if !masked {
			return nil, errors.New("client frame not masked")
		}
		if n > wsMaxMessage || uint64(len(msg))+n > wsMaxMessage {
			return nil, errors.New("message too large")
		}
		var mask [4]byte
		if _, err := io.ReadFull(c.r, mask[:]); err != nil {
			return nil, err
		}
		payload := make([]byte, n)
		if _, err := io.ReadFull(c.r, payload); err != nil {
			return nil, err
		}
		for i := range payload {
			payload[i] ^= mask[i%4]
		}
		switch op {
		case 0x8: // close
			c.writeFrame(0x8, nil)
			return nil, errWsClosed
		case 0x9: // ping
			if err := c.writeFrame(0xA, payload); err != nil {
				return nil, err
			}
			continue
		case 0xA: // pong
			continue
		case 0x1, 0x0: // text, continuation
			msg = append(msg, payload...)
			if fin {
				return msg, nil
			}
		default: // binary or unknown: not part of this protocol
			return nil, errors.New("unsupported frame")
		}
	}
}

func (c *wsConn) Close() error {
	c.writeFrame(0x8, nil)
	return c.conn.Close()
}
