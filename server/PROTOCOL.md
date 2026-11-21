# ttt2d protocol (version 1)

`ttt2d` is the TTT2 Client's matchmaking and relay server. It never runs the game: it pairs players,
helps their PCs connect to each other, relays inputs when they cannot, and keeps ratings.

## Transports

| What | Where | Notes |
|---|---|---|
| Control | `wss://<server>:443/v1` | WebSocket over TLS, text frames, one JSON object per frame. The certificate is self-signed; the client pins its SHA-256 fingerprint (printed by `ttt2d -fingerprint`). |
| UDP | `<server>:27620` | Address reflection (for hole punching) and the relay. |

## Identity

No Steam. On first start the client asks for a name and sends `register`; the server answers with a
random 32-byte **token** (hex). The client keeps it (it is the account) and sends it in `hello` from then on.
The server stores only the token's SHA-256.

Names: 3 to 16 characters, `A-Z a-z 0-9 _ -`, unique ignoring case.

## Messages, client to server

```
{"t":"hello","v":1,"token":"<hex>","client":"0.2.0"}
{"t":"register","v":1,"name":"Mumbai","client":"0.2.0"}
{"t":"queue","mode":"ranked"|"casual","side":"p1"|"p2"|"any","maxPing":120}
{"t":"cancel"}
{"t":"check","match":"<id>","ok":true,"rtt":38,"direct":true}
{"t":"result","match":"<id>","winner":"me"|"opponent"}
{"t":"leave","match":"<id>"}
{"t":"leaderboard"}
{"t":"ping"}
```

## Messages, server to client

```
{"t":"welcome","id":"<id>","name":"Mumbai","rating":1500,"games":0,"wins":0,"udpToken":"<hex>"}
{"t":"registered","token":"<hex>", ...welcome fields}
{"t":"error","code":"name_taken"|"bad_name"|"bad_token"|"outdated"|"busy"|"bad_request","message":"..."}
{"t":"queued","mode":"ranked","side":"any"}
{"t":"stats","online":12,"queued":3,"playing":4}
{"t":"proposal","match":"<id>","mode":"ranked","side":"p1"|"p2",
 "opponent":{"name":"X","rating":1620},
 "peer":{"addr":"203.0.113.5:51234"},
 "relay":{"port":27620,"token":"<hex>"},
 "checkSeconds":8}
{"t":"go","match":"<id>","side":"p1"|"p2","direct":true,"seed":123456789}
{"t":"cancelled","match":"<id>","reason":"opponent_left"|"too_far"|"check_failed"|"timeout"}
{"t":"rated","match":"<id>","rating":1516,"change":16,"games":1,"wins":1}
{"t":"left","match":"<id>"}                       (the opponent left the match)
{"t":"leaderboard","rows":[{"rank":1,"name":"X","rating":1700,"games":40,"wins":30}]}
{"t":"pong"}
```

## Matching

* Players queue for **ranked** or **casual** with a side: `p1`, `p2` or `any`.
* **Side lock is hard.** Two players who both locked `p1` (or both `p2`) are never paired. A locked
  player always gets their side; `any` takes what is left; two `any` players get a random split.
* Ranked pairs by rating: the window starts at ±100 and widens by 25 per second waited (up to ±1000).
* A pairing is only a **proposal** until the two PCs have tested their connection: both clients punch
  through to each other (and fall back to the relay), measure the round trip and send `check`. The match
  goes ahead (`go`) only if both checks pass and the round trip is within both players' `maxPing`, which
  each client widens itself while it waits. A failed pair is not proposed again for 10 minutes; both
  players return to the queue keeping their waiting time.

## UDP packets

Every packet starts with a type byte and the player's 16-byte `udpToken` (from `welcome`), or for the
relay the 16-byte relay token from `proposal`.

| Type | Direction | Body |
|---|---|---|
| `0x01` reflect | client → server | token |
| `0x02` reflected | server → client | the client's public address as text, e.g. `203.0.113.5:51234` |
| `0x03` relay | client → server | token + payload (≤ 1200 bytes) |
| `0x04` relayed | server → client | payload from the other player |

Clients send a reflect packet every 2 seconds while queued, so the server knows each player's public UDP
address to hand to their opponent.

## Results

After a match both clients send `result`. Ranked ratings change (Elo, K = 40 for a player's first 20
games, then 24) when both reports agree. If they disagree nothing changes and the match is logged as
disputed. If one player disconnects without reporting, the report of the one who stayed counts after 2 minutes.
