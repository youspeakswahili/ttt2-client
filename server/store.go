package main

// Players and match history on disk: players.json (rewritten atomically, at most once a second) and
// matches.jsonl (one line per finished match, appended).

import (
	"crypto/rand"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"sync"
	"time"
)

type Player struct {
	ID        string    `json:"id"`
	Name      string    `json:"name"`
	TokenHash string    `json:"tokenHash"`
	Rating    int       `json:"rating"`
	Games     int       `json:"games"`
	Wins      int       `json:"wins"`
	Created   time.Time `json:"created"`
	LastSeen  time.Time `json:"lastSeen"`
}

type MatchRecord struct {
	ID       string    `json:"id"`
	Mode     string    `json:"mode"`
	P1       string    `json:"p1"`
	P2       string    `json:"p2"`
	Winner   string    `json:"winner,omitempty"` // player id; empty when disputed / unknown
	Outcome  string    `json:"outcome"`          // agreed, disputed, forfeit, abandoned
	P1Change int       `json:"p1Change"`
	P2Change int       `json:"p2Change"`
	Started  time.Time `json:"started"`
	Ended    time.Time `json:"ended"`
	RTT      int       `json:"rtt"`
	Direct   bool      `json:"direct"`
}

type Store struct {
	mu      sync.Mutex
	dir     string
	byID    map[string]*Player
	byToken map[string]*Player
	byName  map[string]*Player // lower-case
	dirty   bool
}

var nameRe = regexp.MustCompile(`^[A-Za-z0-9_-]{3,16}$`)

var (
	errBadName   = errors.New("names are 3-16 letters, digits, _ or -")
	errNameTaken = errors.New("that name is taken")
	errBadToken  = errors.New("unknown account")
)

func randHex(n int) string {
	b := make([]byte, n)
	if _, err := rand.Read(b); err != nil {
		panic(err)
	}
	return hex.EncodeToString(b)
}

func tokenHash(token string) string {
	s := sha256.Sum256([]byte(token))
	return hex.EncodeToString(s[:])
}

func OpenStore(dir string) (*Store, error) {
	if err := os.MkdirAll(dir, 0o700); err != nil {
		return nil, err
	}
	s := &Store{dir: dir, byID: map[string]*Player{}, byToken: map[string]*Player{}, byName: map[string]*Player{}}
	data, err := os.ReadFile(filepath.Join(dir, "players.json"))
	if err != nil && !os.IsNotExist(err) {
		return nil, err
	}
	if len(data) > 0 {
		var list []*Player
		if err := json.Unmarshal(data, &list); err != nil {
			return nil, err
		}
		for _, p := range list {
			s.byID[p.ID] = p
			s.byToken[p.TokenHash] = p
			s.byName[strings.ToLower(p.Name)] = p
		}
	}
	return s, nil
}

// Register makes a new account; returns the player and their secret token.
func (s *Store) Register(name string) (*Player, string, error) {
	if !nameRe.MatchString(name) {
		return nil, "", errBadName
	}
	s.mu.Lock()
	defer s.mu.Unlock()
	if _, taken := s.byName[strings.ToLower(name)]; taken {
		return nil, "", errNameTaken
	}
	token := randHex(32)
	now := time.Now().UTC()
	p := &Player{ID: randHex(8), Name: name, TokenHash: tokenHash(token), Rating: 1500, Created: now, LastSeen: now}
	s.byID[p.ID] = p
	s.byToken[p.TokenHash] = p
	s.byName[strings.ToLower(name)] = p
	s.dirty = true
	return p, token, nil
}

func (s *Store) Login(token string) (*Player, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	p := s.byToken[tokenHash(token)]
	if p == nil {
		return nil, errBadToken
	}
	p.LastSeen = time.Now().UTC()
	s.dirty = true
	return p, nil
}

// Snapshot copies a player's public fields under the lock.
func (s *Store) Snapshot(p *Player) Player {
	s.mu.Lock()
	defer s.mu.Unlock()
	return *p
}

// ApplyResult updates both ratings (Elo) for a ranked match; returns the changes.
func (s *Store) ApplyResult(winner, loser *Player) (int, int) {
	s.mu.Lock()
	defer s.mu.Unlock()
	dw, dl := eloChange(winner.Rating, loser.Rating, kFactor(winner.Games), kFactor(loser.Games))
	winner.Rating += dw
	loser.Rating += dl
	winner.Games++
	loser.Games++
	winner.Wins++
	s.dirty = true
	return dw, dl
}

func (s *Store) AppendMatch(m MatchRecord) {
	b, _ := json.Marshal(m)
	f, err := os.OpenFile(filepath.Join(s.dir, "matches.jsonl"), os.O_CREATE|os.O_APPEND|os.O_WRONLY, 0o600)
	if err != nil {
		return
	}
	f.Write(append(b, '\n'))
	f.Close()
}

type BoardRow struct {
	Rank   int    `json:"rank"`
	Name   string `json:"name"`
	Rating int    `json:"rating"`
	Games  int    `json:"games"`
	Wins   int    `json:"wins"`
}

// Leaderboard: players with at least 5 ranked games, best first.
func (s *Store) Leaderboard(limit int) []BoardRow {
	s.mu.Lock()
	list := make([]Player, 0, len(s.byID))
	for _, p := range s.byID {
		if p.Games >= 5 {
			list = append(list, *p)
		}
	}
	s.mu.Unlock()
	sort.Slice(list, func(i, j int) bool {
		if list[i].Rating != list[j].Rating {
			return list[i].Rating > list[j].Rating
		}
		return list[i].Games > list[j].Games
	})
	rows := []BoardRow{}
	for i, p := range list {
		if i >= limit {
			break
		}
		rows = append(rows, BoardRow{Rank: i + 1, Name: p.Name, Rating: p.Rating, Games: p.Games, Wins: p.Wins})
	}
	return rows
}

// Flush writes players.json if anything changed.
func (s *Store) Flush() error {
	s.mu.Lock()
	if !s.dirty {
		s.mu.Unlock()
		return nil
	}
	list := make([]*Player, 0, len(s.byID))
	for _, p := range s.byID {
		c := *p
		list = append(list, &c)
	}
	s.dirty = false
	s.mu.Unlock()
	sort.Slice(list, func(i, j int) bool { return list[i].Created.Before(list[j].Created) })
	data, err := json.MarshalIndent(list, "", " ")
	if err != nil {
		return err
	}
	tmp := filepath.Join(s.dir, "players.json.tmp")
	if err := os.WriteFile(tmp, data, 0o600); err != nil {
		return err
	}
	return os.Rename(tmp, filepath.Join(s.dir, "players.json"))
}
