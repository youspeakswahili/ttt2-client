package main

import "math"

// kFactor: new players move fast, settled ones slower.
func kFactor(games int) float64 {
	if games < 20 {
		return 40
	}
	return 24
}

// eloChange returns the winner's gain and the loser's (negative) change. Every result moves a rating
// by at least 1 point.
func eloChange(winner, loser int, kw, kl float64) (int, int) {
	expectW := 1 / (1 + math.Pow(10, float64(loser-winner)/400))
	gain := int(math.Round(kw * (1 - expectW)))
	loss := int(math.Round(kl * (1 - expectW)))
	if gain < 1 {
		gain = 1
	}
	if loss < 1 {
		loss = 1
	}
	return gain, -loss
}
