package main

// The lobby: who is online, the ranked and casual queues, proposals (paired but not yet connected),
// matches being played, and their results. One mutex; the matchmaker runs twice a second.

import (
	"encoding/json"
	"log"
	"math/rand/v2"
	"sort"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	checkSeconds    = 8                // what the clients get to test their connection
	proposalTimeout = 12 * time.Second // check time plus slack
	failedPairWait  = 20 * time.Second // a pair whose proposal fell through waits this long (few players online: keep it short)
	forfeitAfter    = 2 * time.Minute
	matchMaxLength  = 90 * time.Minute
	relayTTL        = 3 * time.Hour
	minClient       = "0.2.0"
)

type Session struct {
	lobby    *Lobby
	conn     *wsConn
	player   *Player
	udpToken string
	send     chan []byte
	closed   bool
	queue    *QueueEntry
	match    *Match
}

type QueueEntry struct {
	s       *Session
	mode    string // ranked, casual
	side    string // p1, p2, any
	maxPing int
	since   time.Time
}

type check struct {
	done, ok, direct bool
	rtt              int
}

type Match struct {
	id       string
	mode     string
	players  [2]*Player    // [0] stands on the 1P side
	sessions [2]*Session   // nil while that player is disconnected
	entries  [2]QueueEntry // to requeue with their waiting time kept
	state    string        // proposal, playing, done
	created  time.Time
	started  time.Time
	checks   [2]check
	relay    [2]string
	reports  [2]string // winner's player id, as each side reported it
	left     [2]time.Time
	rtt      int
	direct   bool
}

type pairKey struct{ a, b string }

func keyOf(a, b string) pairKey {
	if a > b {
		a, b = b, a
	}
	return pairKey{a, b}
}

type Lobby struct {
	mu       sync.Mutex
	store    *Store
	udp      *UDPServer
	sessions map[string]*Session // by player id
	queue    []*QueueEntry
	matches  map[string]*Match
	failed   map[pairKey]time.Time
}

func NewLobby(store *Store, udp *UDPServer) *Lobby {
	return &Lobby{store: store, udp: udp, sessions: map[string]*Session{}, matches: map[string]*Match{}, failed: map[pairKey]time.Time{}}
}

// ------------------------------------------------------------------ sending

func (s *Session) Send(v any) {
	b, err := json.Marshal(v)
	if err != nil || s.closed {
		return
	}
	select {
	case s.send <- b:
	default: // a client that does not read: let it go
		s.closed = true
		close(s.send)
	}
}

func (s *Session) sendError(code, msg string) {
	s.Send(map[string]any{"t": "error", "code": code, "message": msg})
}

func (s *Session) writer() {
	for b := range s.send {
		if err := s.conn.WriteText(b); err != nil {
			break
		}
	}
	s.conn.Close()
}

// ------------------------------------------------------------------ connections

func versionAtLeast(have, want string) bool {
	parse := func(v string) [3]int {
		var out [3]int
		for i, p := range strings.SplitN(strings.TrimPrefix(v, "v"), ".", 3) {
			n, _ := strconv.Atoi(strings.TrimFunc(p, func(r rune) bool { return r < '0' || r > '9' }))
			out[i] = n
		}
		return out
	}
	a, b := parse(have), parse(want)
	for i := 0; i < 3; i++ {
		if a[i] != b[i] {
			return a[i] > b[i]
		}
	}
	return true
}

type inMsg struct {
	T       string `json:"t"`
	V       int    `json:"v"`
	Token   string `json:"token"`
	Name    string `json:"name"`
	Client  string `json:"client"`
	Mode    string `json:"mode"`
	Side    string `json:"side"`
	MaxPing int    `json:"maxPing"`
	Match   string `json:"match"`
	OK      bool   `json:"ok"`
	RTT     int    `json:"rtt"`
	Direct  bool   `json:"direct"`
	Winner  string `json:"winner"`
}

// Serve runs one client connection until it closes.
func (l *Lobby) Serve(c *wsConn, remote string) {
	s := &Session{lobby: l, conn: c, send: make(chan []byte, 64)}
	go s.writer()
	defer func() {
		l.mu.Lock()
		l.disconnect(s)
		if !s.closed {
			s.closed = true
			close(s.send)
		}
		l.mu.Unlock()
	}()

	// first message: hello or register, within 15 seconds
	raw, err := c.ReadText(15 * time.Second)
	if err != nil {
		return
	}
	var m inMsg
	if json.Unmarshal(raw, &m) != nil || (m.T != "hello" && m.T != "register") {
		s.sendError("bad_request", "say hello first")
		return
	}
	if !versionAtLeast(m.Client, minClient) {
		s.sendError("outdated", "please update the client (this server needs "+minClient+" or newer)")
		return
	}
	var token string
	var p *Player
	if m.T == "register" {
		p, token, err = l.store.Register(strings.TrimSpace(m.Name))
		if err == errBadName {
			s.sendError("bad_name", err.Error())
			return
		} else if err == errNameTaken {
			s.sendError("name_taken", err.Error())
			return
		} else if err != nil {
			s.sendError("busy", "try again")
			return
		}
	} else if p, err = l.store.Login(m.Token); err != nil {
		s.sendError("bad_token", "this account is not known to the server")
		return
	}

	l.mu.Lock()
	if old := l.sessions[p.ID]; old != nil { // signed in somewhere else: the newest wins
		old.sendError("busy", "signed in from another place")
		l.disconnect(old)
		if !old.closed {
			old.closed = true
			close(old.send)
		}
	}
	s.player = p
	s.udpToken = randHex(16)
	l.udp.AddPlayer(s.udpToken)
	l.sessions[p.ID] = s
	snap := l.store.Snapshot(p)
	welcome := map[string]any{"t": "welcome", "id": snap.ID, "name": snap.Name, "rating": snap.Rating, "games": snap.Games,
		"wins": snap.Wins, "udpToken": s.udpToken, "udpPort": l.udp.Port()}
	if token != "" {
		welcome["t"] = "registered"
		welcome["token"] = token
	}
	s.Send(welcome)
	l.reattach(s)
	l.mu.Unlock()
	log.Printf("%s signed in (%s)", snap.Name, remote)

	for {
		raw, err := c.ReadText(60 * time.Second)
		if err != nil {
			return
		}
		var m inMsg
		if json.Unmarshal(raw, &m) != nil {
			continue
		}
		l.mu.Lock()
		l.handle(s, &m)
		l.mu.Unlock()
	}
}

func (l *Lobby) handle(s *Session, m *inMsg) {
	switch m.T {
	case "ping":
		s.Send(map[string]any{"t": "pong"})
	case "queue":
		if s.match != nil {
			s.sendError("busy", "you are in a match")
			return
		}
		if m.Mode != "ranked" && m.Mode != "casual" {
			m.Mode = "casual"
		}
		if m.Side != "p1" && m.Side != "p2" {
			m.Side = "any"
		}
		if m.MaxPing <= 0 {
			m.MaxPing = 150
		}
		m.MaxPing = min(max(m.MaxPing, 30), 400)
		if s.queue != nil {
			l.unqueue(s)
		}
		s.queue = &QueueEntry{s: s, mode: m.Mode, side: m.Side, maxPing: m.MaxPing, since: time.Now()}
		l.queue = append(l.queue, s.queue)
		s.Send(map[string]any{"t": "queued", "mode": m.Mode, "side": m.Side})
	case "cancel":
		l.unqueue(s)
	case "check":
		l.onCheck(s, m)
	case "result":
		l.onResult(s, m)
	case "leaderboard":
		s.Send(map[string]any{"t": "leaderboard", "rows": l.store.Leaderboard(100)})
	case "leave":
		l.onLeave(s, m)
	}
}

func (l *Lobby) unqueue(s *Session) {
	if s.queue == nil {
		return
	}
	for i, e := range l.queue {
		if e == s.queue {
			l.queue = append(l.queue[:i], l.queue[i+1:]...)
			break
		}
	}
	s.queue = nil
}

func (l *Lobby) disconnect(s *Session) {
	if s.player == nil || l.sessions[s.player.ID] != s {
		return
	}
	delete(l.sessions, s.player.ID)
	l.udp.RemovePlayer(s.udpToken)
	l.unqueue(s)
	if m := s.match; m != nil {
		slot := m.slotOf(s.player)
		m.sessions[slot] = nil
		s.match = nil
		switch m.state {
		case "proposal":
			l.cancel(m, "opponent_left", slot)
		case "playing":
			m.left[slot] = time.Now()
		}
	}
	log.Printf("%s signed out", s.player.Name)
}

// reattach: a player who comes back during their match can still report its result.
func (l *Lobby) reattach(s *Session) {
	for _, m := range l.matches {
		if m.state != "playing" {
			continue
		}
		for i, p := range m.players {
			if p.ID == s.player.ID && m.sessions[i] == nil {
				m.sessions[i] = s
				m.left[i] = time.Time{}
				s.match = m
			}
		}
	}
}

func (m *Match) slotOf(p *Player) int {
	if m.players[1].ID == p.ID {
		return 1
	}
	return 0
}

// ------------------------------------------------------------------ matchmaking

func compatibleSides(a, b string) bool { return !(a == b && a != "any") }

// assignSides: a locked player always gets their side; "any" takes what is left; two "any": random.
func assignSides(a, b *QueueEntry) (p1, p2 *QueueEntry) {
	switch {
	case a.side == "p1" || b.side == "p2":
		return a, b
	case a.side == "p2" || b.side == "p1":
		return b, a
	case rand.IntN(2) == 0:
		return a, b
	default:
		return b, a
	}
}

func ratingWindow(wait time.Duration) int {
	return min(100+int(wait.Seconds())*25, 1000)
}

// Tick pairs players and handles timeouts. Called twice a second.
func (l *Lobby) Tick(now time.Time) {
	l.mu.Lock()
	defer l.mu.Unlock()
	for k, t := range l.failed {
		if now.Sub(t) > failedPairWait {
			delete(l.failed, k)
		}
	}
	// oldest first; each finds its closest-rated compatible partner
	sort.SliceStable(l.queue, func(i, j int) bool { return l.queue[i].since.Before(l.queue[j].since) })
	taken := map[*QueueEntry]bool{}
	for i, a := range l.queue {
		if taken[a] {
			continue
		}
		var best *QueueEntry
		bestDelta := 1 << 30
		for _, b := range l.queue[i+1:] {
			if taken[b] || b.mode != a.mode || !compatibleSides(a.side, b.side) {
				continue
			}
			if _, bad := l.failed[keyOf(a.s.player.ID, b.s.player.ID)]; bad {
				continue
			}
			delta := a.s.player.Rating - b.s.player.Rating
			if delta < 0 {
				delta = -delta
			}
			if a.mode == "ranked" && delta > ratingWindow(now.Sub(a.since)) {
				continue
			}
			if delta < bestDelta {
				best, bestDelta = b, delta
			}
		}
		if best != nil {
			taken[a], taken[best] = true, true
		}
		if best != nil {
			l.propose(a, best, now)
		}
	}
	if len(taken) > 0 {
		rest := l.queue[:0]
		for _, e := range l.queue {
			if !taken[e] {
				rest = append(rest, e)
			}
		}
		l.queue = rest
	}

	for _, m := range l.matches {
		switch m.state {
		case "proposal":
			if now.Sub(m.created) > proposalTimeout {
				// whoever never answered is taken out of the queue (probably away); the other goes back
				silent := -1
				for i := range m.checks {
					if !m.checks[i].done {
						silent = i
					}
				}
				l.cancel(m, "timeout", silent)
			}
		case "playing":
			l.checkForfeit(m, now)
			if m.state == "playing" && now.Sub(m.started) > matchMaxLength {
				l.finish(m, "", "abandoned")
			}
		}
	}
}

func (l *Lobby) propose(a, b *QueueEntry, now time.Time) {
	p1, p2 := assignSides(a, b)
	m := &Match{id: randHex(8), mode: a.mode, state: "proposal", created: now,
		players: [2]*Player{p1.s.player, p2.s.player}, sessions: [2]*Session{p1.s, p2.s}, entries: [2]QueueEntry{*p1, *p2}}
	m.relay[0], m.relay[1] = l.udp.NewRelay(relayTTL)
	l.matches[m.id] = m
	for i, s := range m.sessions {
		s.queue = nil
		s.match = m
		other := m.sessions[1-i]
		op := l.store.Snapshot(other.player)
		s.Send(map[string]any{
			"t": "proposal", "match": m.id, "mode": m.mode, "side": []string{"p1", "p2"}[i],
			"opponent":     map[string]any{"name": op.Name, "rating": op.Rating},
			"peer":         map[string]any{"addr": l.udp.PlayerAddr(other.udpToken)},
			"relay":        map[string]any{"port": l.udp.Port(), "token": m.relay[i]},
			"checkSeconds": checkSeconds,
		})
	}
	log.Printf("proposal %s: %s (1P) vs %s (2P), %s", m.id, m.players[0].Name, m.players[1].Name, m.mode)
}

func (l *Lobby) onCheck(s *Session, msg *inMsg) {
	m := s.match
	if m == nil || m.id != msg.Match || m.state != "proposal" {
		return
	}
	slot := m.slotOf(s.player)
	m.checks[slot] = check{done: true, ok: msg.OK, rtt: max(msg.RTT, 0), direct: msg.Direct}
	if !m.checks[0].done || !m.checks[1].done {
		return
	}
	rtt := max(m.checks[0].rtt, m.checks[1].rtt)
	limit := min(m.entries[0].maxPing, m.entries[1].maxPing)
	switch {
	case !m.checks[0].ok || !m.checks[1].ok:
		l.cancel(m, "check_failed", -1)
	case rtt > limit:
		l.cancel(m, "too_far", -1)
	default:
		m.state, m.started, m.rtt = "playing", time.Now(), rtt
		m.direct = m.checks[0].direct && m.checks[1].direct
		seed := rand.Uint32()
		for i, ss := range m.sessions {
			if ss != nil {
				ss.Send(map[string]any{"t": "go", "match": m.id, "side": []string{"p1", "p2"}[i], "direct": m.direct, "seed": seed, "rtt": rtt})
			}
		}
		log.Printf("match %s starts: %d ms, direct=%v", m.id, rtt, m.direct)
	}
}

// cancel ends a proposal. Both players go back to the queue keeping their waiting time, except `drop`
// (a slot that left or never answered; -1 = none). The pair is not proposed again for a while.
func (l *Lobby) cancel(m *Match, reason string, drop int) {
	m.state = "done"
	delete(l.matches, m.id)
	l.udp.CloseRelay(m.relay[0], m.relay[1])
	l.failed[keyOf(m.players[0].ID, m.players[1].ID)] = time.Now()
	for i, s := range m.sessions {
		if s == nil {
			continue
		}
		s.match = nil
		s.Send(map[string]any{"t": "cancelled", "match": m.id, "reason": reason})
		if i == drop {
			continue
		}
		e := m.entries[i]
		e.s = s
		s.queue = &e
		l.queue = append(l.queue, s.queue)
		s.Send(map[string]any{"t": "queued", "mode": e.mode, "side": e.side})
	}
}

// onLeave: a player backs out of a found match. In a proposal the other player goes back to the queue; in a match it
// counts as leaving (forfeit if the other reports a win, abandoned if both leave), at once instead of after 2 minutes.
func (l *Lobby) onLeave(s *Session, msg *inMsg) {
	m := s.match
	if m == nil || m.id != msg.Match {
		return
	}
	slot := m.slotOf(s.player)
	switch m.state {
	case "proposal":
		m.sessions[slot] = nil
		s.match = nil
		l.cancel(m, "opponent_left", slot)
	case "playing":
		m.sessions[slot] = nil
		s.match = nil
		m.left[slot] = time.Now().Add(-forfeitAfter - time.Second)
		if other := m.sessions[1-slot]; other != nil {
			other.Send(map[string]any{"t": "left", "match": m.id})
		}
		l.checkForfeit(m, time.Now())
	}
}

// ------------------------------------------------------------------ results

func (l *Lobby) onResult(s *Session, msg *inMsg) {
	m := s.match
	if m == nil || m.id != msg.Match || m.state != "playing" {
		return
	}
	slot := m.slotOf(s.player)
	if msg.Winner == "me" {
		m.reports[slot] = m.players[slot].ID
	} else {
		m.reports[slot] = m.players[1-slot].ID
	}
	if m.reports[0] == "" || m.reports[1] == "" {
		return
	}
	if m.reports[0] == m.reports[1] {
		l.finish(m, m.reports[0], "agreed")
	} else {
		l.finish(m, "", "disputed")
	}
}

func (l *Lobby) checkForfeit(m *Match, now time.Time) {
	for i := 0; i < 2; i++ {
		gone := !m.left[i].IsZero() && now.Sub(m.left[i]) > forfeitAfter
		if !gone || m.reports[i] != "" {
			continue
		}
		if m.reports[1-i] != "" && m.sessions[1-i] != nil {
			l.finish(m, m.reports[1-i], "forfeit")
		} else if !m.left[1-i].IsZero() {
			l.finish(m, "", "abandoned")
		}
		return
	}
}

func (l *Lobby) finish(m *Match, winnerID, outcome string) {
	m.state = "done"
	delete(l.matches, m.id)
	l.udp.CloseRelay(m.relay[0], m.relay[1])
	rec := MatchRecord{ID: m.id, Mode: m.mode, P1: m.players[0].ID, P2: m.players[1].ID, Winner: winnerID, Outcome: outcome,
		Started: m.started, Ended: time.Now().UTC(), RTT: m.rtt, Direct: m.direct}
	changes := [2]int{}
	if m.mode == "ranked" && winnerID != "" {
		w := 0
		if m.players[1].ID == winnerID {
			w = 1
		}
		gain, loss := l.store.ApplyResult(m.players[w], m.players[1-w])
		changes[w], changes[1-w] = gain, loss
	}
	rec.P1Change, rec.P2Change = changes[0], changes[1]
	l.store.AppendMatch(rec)
	for i, s := range m.sessions {
		if s == nil {
			continue
		}
		s.match = nil
		p := l.store.Snapshot(m.players[i])
		s.Send(map[string]any{"t": "rated", "match": m.id, "outcome": outcome, "rating": p.Rating, "change": changes[i], "games": p.Games, "wins": p.Wins})
	}
	log.Printf("match %s: %s", m.id, outcome)
}

// Stats to everyone, every few seconds.
func (l *Lobby) BroadcastStats() {
	l.mu.Lock()
	defer l.mu.Unlock()
	playing := 0
	for _, m := range l.matches {
		if m.state == "playing" {
			playing += 2
		}
	}
	msg := map[string]any{"t": "stats", "online": len(l.sessions), "queued": len(l.queue), "playing": playing}
	for _, s := range l.sessions {
		s.Send(msg)
	}
}
