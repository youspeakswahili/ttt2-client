# TTT2 Client

<img src="art/logo.png" width="128" align="right" alt="TTT2 Client logo">

A launcher for the [Tekken Tag Tournament 2 PC port](https://github.com/shihabal3amri/tekken-tag-tournament-2-pc):

- **Install**: builds the PC version from *your own* Xbox 360 disc image and Title Update 3, using the port's official release.
- **Controls**: every controller works (PlayStation pads, fight sticks, hitboxes, generic sticks). Press-to-map every
  button, Tag and macros (1+2, 3+4, 1+3, 2+4, 1+3+4, 1+2+3+4), SOCD options for hitboxes, live input test.
- **Settings**: the game's input delay, low latency, netcode and display settings, plus full screen for the client.
- **Play**: launch the game, ready check.

Coming next: matchmaking from the client (ranked and casual queues, hard side lock, practice while you wait),
through our own server instead of Steam.

**No game files are included or downloaded by this project.** You need your own copy of the game.
This project is not affiliated with Bandai Namco or with the port's author.

## Download

Grab the latest zip from [Releases](../../releases), extract it, run `TTT2 Client.exe`.
Windows SmartScreen may warn the first time (the exe is not code-signed): **More info → Run anyway**.

## What's in here

| Folder | What |
|---|---|
| `client/` | The client: SDL3 + Dear ImGui, Direct3D 11 |
| `input/` | `xinput9_1_0.dll`, the controller layer placed next to the game's exe: reads every controller through SDL3 |
| `common/` | The controller map format shared by both |
| `server/` | `ttt2d`, the matchmaking and relay server (Go, standard library only). See `server/PROTOCOL.md` |
| `tests/`, `server/*_test.go` | Tests |
| `art/` | The logo, drawn with Python (Pillow + NumPy) |
| `licenses/` | Third-party licenses (SDL3, Dear ImGui, Inter, Barlow Condensed) |

## Building

The Windows client is cross-compiled from Linux with [llvm-mingw](https://github.com/mstorsjo/llvm-mingw) and a static
SDL3 3.2: `./build_windows.sh` (set `W` to a work folder holding `sdl-install/`). The server: `cd server && go test ./... && go build`.
