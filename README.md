# TTT2 Client

<img src="art/logo.png" width="128" align="right" alt="TTT2 Client logo">

A launcher for the [Tekken Tag Tournament 2 PC port](https://github.com/shihabal3amri/tekken-tag-tournament-2-pc):

- **Install**: builds the PC version from *your own* Xbox 360 disc image and Title Update 3, using the port's official release.
- **Controls**: every controller works (PlayStation pads, fight sticks, hitboxes, generic sticks). Press-to-map every
  button, Tag and macros (1+2, 3+4, 1+3, 2+4, 1+3+4, 1+2+3+4), SOCD options for hitboxes, live input test.
- **Settings**: the game's input delay, low latency, netcode and display settings, plus full screen for the client.
- **Play**: launch the game, ready check.

- **Online**: an account on the TTT2 server (no email or password), ranked and casual queues with a guaranteed
  side (1P / 2P / either) and a ping limit, a connection check between the two PCs (hole punching, relay
  fallback), ratings and a leaderboard.

Coming next: the game starting straight into a found match (and practice while you wait), over our own
connection instead of Steam.

**No game files are included or downloaded by this project.** You need your own copy of the game.
This project is not affiliated with Bandai Namco or with the port's author.

## Download

Grab the latest zip from [Releases](../../releases), extract it, run `TTT2 Client.exe`.
**New here? Follow the [setup guide](SETUP.md)**: what you need, building the game, controllers and online.
Windows SmartScreen may warn the first time (the exe is not code-signed): **More info → Run anyway**.

## Updates

The client checks this repository's latest release when it starts and offers to update itself. Every release zip
carries `manifest.txt` (each file's SHA-512) and `manifest.sig` (an Ed25519 signature by the project's release key);
the client installs nothing that is not signed by that key and unchanged. Making a release:

```
go run ./tools/release -key <path to release-signing.key> -dir "pkg/TTT2 Client" -version 0.1.8 -out TTT2-Client-0.1.8.zip
```

Bump `client/version.h` first, tag the release `v0.1.8`, attach the zip. The private key never goes in this repository.

## What's in here

| Folder | What |
|---|---|
| `client/` | The client: SDL3 + Dear ImGui, Direct3D 11 |
| `input/` | `xinput9_1_0.dll`, the controller layer placed next to the game's exe: reads every controller through SDL3 |
| `common/` | The controller map format shared by both |
| `server/` | `ttt2d`, the matchmaking and relay server (Go, standard library only). See `server/PROTOCOL.md` |
| `tests/`, `server/*_test.go` | Tests |
| `art/` | The logo, drawn with Python (Pillow + NumPy) |
| `tools/release/` | Signs and zips a release |
| `licenses/` | Third-party licenses (SDL3, Dear ImGui, Inter, Barlow Condensed, Monocypher, miniz) |

## Building

The Windows client is cross-compiled from Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) and a static
SDL3 3.2: `./build_windows.sh` (set `W` to a work folder holding `sdl-install/`). The server: `cd server && go test ./... && go build`.
