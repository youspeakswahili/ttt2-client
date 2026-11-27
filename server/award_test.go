package main

import (
	"bufio"
	"compress/gzip"
	"encoding/json"
	"os"
	"testing"
)

// Fights run through the game's own award code (tests/rank_emu: rank_emu fuzz N file): Award must give the same.
type awardCase struct {
	Winner int  `json:"winner"`
	Draw   bool `json:"draw"`
	Before [2]struct {
		C [2]int    `json:"c"`
		E [2][5]int `json:"e"`
	} `json:"before"`
	Slots [2]struct {
		S [2][5]int `json:"s"`
	} `json:"slots"`
}

func toTeam(e [2][5]int, solo bool) Team {
	var t Team
	for i := range t {
		src := e[i]
		if solo {
			src = e[0]
		}
		t[i] = Fighter{src[0], src[1], src[2], src[3], src[4]}
	}
	return t
}

func TestAwardMatchesTheGame(t *testing.T) {
	f, err := os.Open("testdata/award_cases.jsonl.gz")
	if err != nil {
		t.Fatal(err)
	}
	defer f.Close()
	gz, err := gzip.NewReader(f)
	if err != nil {
		t.Fatal(err)
	}
	sc := bufio.NewScanner(gz)
	sc.Buffer(make([]byte, 1<<20), 1<<20)
	n, bad := 0, 0
	for sc.Scan() {
		var c awardCase
		if err := json.Unmarshal(sc.Bytes(), &c); err != nil {
			t.Fatal(err)
		}
		n++
		var teams [2]Team
		for s := 0; s < 2; s++ {
			teams[s] = toTeam(c.Before[s].E, c.Before[s].C[0] == c.Before[s].C[1])
		}
		Award(&teams[c.Winner], &teams[1-c.Winner], c.Draw)
		for s := 0; s < 2; s++ {
			for i := 0; i < 2; i++ {
				got := teams[s][i]
				w := c.Slots[s].S[i]
				want := Fighter{w[0], w[1], w[2], w[3], w[4]}
				if got != want {
					bad++
					if bad <= 8 {
						t.Errorf("case %d side %d slot %d: got %+v want %+v (before %v, winner %d)", n, s, i, got, want, c.Before, c.Winner)
					}
				}
			}
		}
	}
	if bad > 0 {
		t.Fatalf("%d of %d fights differ from the game", bad, n)
	}
	t.Logf("%d fights match the game", n)
}
