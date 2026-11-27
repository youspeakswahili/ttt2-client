package main

// The game's own Ranked Match rules, so the server works out every ranked result itself instead of trusting a
// player's PC. Written from the game's award (Xbox 360, title update 3) and checked against it: testdata holds random
// fights run through the game's real code (tests/rank_emu fuzz), and award_test.go needs every field to match.
//
// Each character has a rank (0 Beginner .. 42 True Tekken God), points, a streak (wins > 0, losses < 0) and two bars
// that Tekken Lord / Tekken God promotion needs. A fight puts each team's two characters in two fighter slots (SOLO:
// the same character twice; it keeps what the first slot ends with).

// Fighter is one character's ranked state.
type Fighter struct {
	Rank   int `json:"rank"`
	Points int `json:"points"`
	Streak int `json:"streak"`
	Bar1   int `json:"bar1"`
	Bar2   int `json:"bar2"`
}

// Team is a side's two fighter slots.
type Team [2]Fighter

// the game's rank table: the points a rank starts with, and the points that promote out of it
var rankStart = [43]int{0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 8000, 8000, 8000, 8000, 8000, 8000, 12000, 7000, 7000, 7000, 10000, 7000, 7000, 7000,
	12000, 8000, 8000, 8000, 11000, 8000, 8000, 8000, 12000, 8000, 8000, 8000, 8000, 12000, 10000, 20000, 20000, 20000}
var rankNext = [43]int{100, 300, 500, 700, 900, 1100, 1300, 1500, 1700, 2000, 4000, 12000, 12000, 13000, 13000, 13000, 16000, 19000, 13000, 14000,
	15000, 17000, 13000, 14000, 19000, 21000, 16000, 17000, 18000, 21000, 18000, 18000, 22000, 22000, 20000, 20000, 20000, 22000, 22000, 30000,
	20000, 20000, 20000}

const (
	maxRank       = 42
	kyuWin        = 200   // a kyu team's win
	kyuLoss       = 50    // a kyu team's loss (the game gives points for it)
	streakRange   = 1     // streaks count against opponents within this many ranks
	streakBonus   = 2000  // the third win in a row
	streakPenalty = -2000 // the third loss in a row
	lordBar       = 30    // Tekken Lord -> Tekken Emperor
	godBar        = 15    // Tekken Emperor -> Tekken God
	pointsLimit   = 31999
)

// points by the rank gap (opponent's rank - own, -3..+3)
var winPoints = [7]int{40, 400, 1000, 2000, 1100, 500, 50}
var lossPoints = [7]int{-50, -500, -1100, -2000, -1000, -400, -40} // by (winner's rank - loser's)

func s16(v int) int { return int(int16(v)) }
func s8(v int) int  { return int(int8(v)) }

func clampPoints(v int) int { return min(max(s16(v), -pointsLimit), pointsLimit) }

// addPoints is the game's saturating add.
func addPoints(p *int, v int) {
	v = s16(v)
	if v >= 0 {
		if *p > pointsLimit-v {
			*p = pointsLimit
		} else {
			*p = s16(*p + v)
		}
	} else if *p < -pointsLimit-v {
		*p = -pointsLimit
	} else {
		*p = s16(*p + v)
	}
}

// lead is the slot with the higher rank (the first on a tie).
func (t *Team) lead() int {
	if t[0].Rank >= t[1].Rank {
		return 0
	}
	return 1
}
func (t *Team) rank() int { return min(t[t.lead()].Rank, 39) }
func (t *Team) kyu() bool { return t[t.lead()].Rank <= 9 }

func gap(d int) int { return min(max(d, -3), 3) }

// Award applies a ranked fight's result to both teams, as the game does. A draw changes nothing.
func Award(w, l *Team, draw bool) {
	if draw {
		return
	}
	// the winner's points
	base, bonus := kyuWin, 0
	if !w.kyu() {
		d := gap(min(l.rank(), 39) - min(w.rank(), 39))
		base = winPoints[d+3]
		if abs(d) <= streakRange {
			f := &w[w.lead()]
			if f.Streak < 0 {
				f.Streak = 1
			} else if f.Streak <= 99 {
				f.Streak++
			}
			if f.Streak == 3 {
				bonus = streakBonus
			}
		}
	}
	total := s16(base + bonus)
	hi := w.lead()
	addPoints(&w[hi].Points, total)
	if f := &w[hi^1]; f.Rank <= 9 {
		addPoints(&f.Points, kyuWin)
	} else {
		addPoints(&f.Points, total)
	}
	// no promotion off a weaker opponent from rank 25 up
	for i := range w {
		f := &w[i]
		if f.Rank >= 25 && f.Rank < 40 && l[l.lead()].Rank < f.Rank && f.Points >= rankNext[f.Rank] {
			f.Points = clampPoints(rankNext[f.Rank] - 1)
		}
	}

	// the loser's points
	base, bonus = kyuLoss, 0
	if !l.kyu() {
		d := gap(min(w.rank(), 39) - min(l.rank(), 39))
		base = lossPoints[d+3]
		if abs(d) <= streakRange {
			f := &l[l.lead()]
			if f.Streak > 0 {
				f.Streak = -1
			} else if f.Streak >= -99 {
				f.Streak--
			}
			if f.Streak == -3 {
				bonus = streakPenalty
			}
		}
	}
	total = s16(base + bonus)
	hi = l.lead()
	addPoints(&l[hi].Points, total)
	if f := &l[hi^1]; f.Rank <= 9 {
		addPoints(&f.Points, kyuLoss)
	} else {
		addPoints(&f.Points, total)
	}

	// the God bars and promotion: the winner, then the loser (who sees the winner's new rank)
	wr, lr := w[w.lead()].Rank, l[l.lead()].Rank
	up1, up2 := 0, 0
	if wr == 40 && lr == 40 {
		up1 = 2
	} else {
		if lr >= 39 {
			up1 = 1
		}
		if lr >= 41 {
			up2 = 1
		}
	}
	for i := range w {
		f := &w[i]
		f.Bar1 = min(f.Bar1+up1, 100)
		f.Bar2 = min(f.Bar2+up2, 100)
		if f.Rank == 40 && f.Bar1 >= lordBar && lr < 40 {
			f.Bar1 = lordBar - 1
		}
	}
	promote(w)

	wr, lr = w[w.lead()].Rank, l[l.lead()].Rank
	down1, down2 := 0, 0
	if wr == 40 && lr == 40 {
		down1 = 2
	} else {
		if wr >= 39 {
			down1 = 1
		}
		if wr >= 41 {
			down2 = 1
		}
	}
	for i := range l {
		f := &l[i]
		f.Bar1 = max(f.Bar1-down1, 0)
		f.Bar2 = max(f.Bar2-down2, 0)
	}
	promote(l)
}

// promote is the game's rank check for both slots: promotion, God-rank promotion by the bars, demotion at zero points.
func promote(t *Team) {
	for i := range t {
		f := &t[i]
		r := f.Rank
		up := r < 40 && f.Points >= rankNext[r]
		if (r == 40 && f.Bar1 >= lordBar) || (r == 41 && f.Bar2 >= godBar) {
			if f.Rank <= maxRank {
				f.Rank++
			}
			f.Points = clampPoints(rankStart[min(f.Rank, maxRank)])
			f.Bar1, f.Bar2 = 0, 0
		}
		switch {
		case up:
			f.Rank++
			switch {
			case f.Rank <= 9:
			case f.Rank == 10:
				f.Points = clampPoints(rankStart[10])
				f.Streak = 0
			default:
				f.Points = clampPoints(rankStart[f.Rank] + min(f.Points-rankNext[f.Rank-1], 1999))
			}
			f.Bar1, f.Bar2 = 0, 0
		case f.Points <= 0:
			switch {
			case f.Rank == 10:
			case f.Rank > 39 && f.Rank <= 42:
				f.Rank -= 2
			case f.Rank >= 1:
				f.Rank--
			}
			if f.Rank == 10 {
				f.Points = clampPoints(rankStart[10])
			} else {
				f.Points = clampPoints(rankStart[f.Rank] + max(f.Points, -1999))
			}
			f.Bar1, f.Bar2 = 0, 0
		}
		if f.Rank >= 40 && f.Points > rankNext[40] {
			f.Points = clampPoints(rankNext[40])
		}
	}
}

func abs(v int) int {
	if v < 0 {
		return -v
	}
	return v
}
