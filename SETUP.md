# Setting up TTT2 Client

This gets you from nothing to playing Tekken Tag Tournament 2 on PC with any controller.
Takes about 15 minutes, most of it the game building itself.

## What you need

- **Windows 10 or 11, 64-bit**, with any DirectX 11 graphics card.
- **About 14 GB free** on the drive you install the game to.
- **Your own Tekken Tag Tournament 2 disc image** (`.iso`) of the retail Xbox 360 release, USA/Europe.
- **Title Update 3** for it. The file is called `TU_174Q22P_0000008000000.00000000000O2`
  (it ends in the letter O, then the number 2). On an Xbox 360 it lives in
  `Content\0000000000000000\4E4D0859\000B0000\`. You can pick the file or the folder it is in.
- **Your DLC** (optional): a folder with your downloaded DLC packages, if you have them.
- **An internet connection** while installing: the client downloads the PC port's installer from GitHub.
- **Any controller.** PlayStation and Xbox pads, fight sticks, hitboxes, generic sticks.
  You don't need DS4Windows or anything else: the client's controller layer handles them.

No game files come with the client, and none are downloaded. The game is built on your PC from your own
disc image and title update. We can't help you get those files, and please don't ask for or share them on the
Discord.

## 1. Download the client

1. Get the latest `TTT2-Client-x.y.z.zip` from [Releases](../../releases/latest).
2. Extract the **whole zip** into its own folder, for example `C:\Games\TTT2 Client`.
   Keep `xinput9_1_0.dll` next to `TTT2 Client.exe`.
3. Run `TTT2 Client.exe`. If Windows says "Windows protected your PC", click **More info → Run anyway**
   (the exe isn't code-signed).

## 2. Build the game (INSTALL page)

1. **Disc image (.iso)**: browse to your disc image.
2. **Title Update 3**: the TU file, or the folder it is in.
3. **Install folder**: an empty folder on a drive with 14 GB free. The page shows how much space that drive has.
4. **DLC folder**: only if you have your DLC. Leave it empty otherwise.
5. Press **BUILD GAME**. Windows asks for permission once; say yes.

The client downloads the latest version of the port, builds the game from your files and adds the controller
layer. It usually takes a few minutes; the log under the progress bar shows what it is doing.
If it says your disc image is the wrong version or damaged, it has to be the USA/Europe retail disc, dumped cleanly.

## 3. Set up your controller (CONTROLS page)

Plug in your pad or stick. It shows up on the left.

- Click a button in the list, then press the button you want for it. Tag and the macros
  (1+2, 3+4, 1+3, 2+4, 1+3+4, 1+2+3+4) can go on any button too.
- Hitboxes: pick how opposite directions are resolved (SOCD).
- Use the input test to check every press lands.

## 4. Settings (SETTINGS page)

- **Lowest input lag** sets the game up for the least delay in one click: the original 2 frames of
  offline input delay removed, low latency on Auto, and online netcode set to prioritise response.
- **Display**: window mode, resolution, anti-aliasing and an FPS counter.

## 5. Play

Go to **PLAY** and press **LAUNCH GAME**. The checklist on that page tells you if anything is missing
(game not built, controller layer not installed).

The first few fights can stutter a little while the game prepares its graphics. That only happens once;
after that it stays smooth.

## Online

On the **ONLINE** page, pick a fighter name (3 to 16 letters, digits, `_` or `-`). There's no email or password:
your account is saved on this PC. You'll see the online count, the leaderboard, and ranked and casual queues where
you choose your side (1P, 2P or either) and the highest ping you'll accept.

Matches started from the client are still in testing and only run on tester builds for now. They'll be
announced on the Discord when they're ready. Until then you can play people through the game's own
**Online Mode**: both players need **Steam running and signed in** and the same version of the game.
Steam shows you as playing "Spacewar". That's expected. Find opponents in `#looking-for-match`.

## Updating

- **The client** checks for a new version every time it starts and offers to update itself. Updates are
  signed, and the client only installs files signed by this project. After an update, Windows may ask for
  permission once so the controller layer in the game folder can be updated too.
- **The game (the port)**: if a newer version of the port comes out, open **INSTALL** and press **REBUILD**
  with the same files.

## When something goes wrong

| Problem | Fix |
|---|---|
| "Windows protected your PC" | **More info → Run anyway**. |
| "xinput9_1_0.dll is missing next to the client" | Extract the whole zip, not just the exe. |
| "the game needs about 13 GB" | Pick an install folder on a drive with more space. |
| The installer rejects the disc image | It has to be the USA/Europe retail disc, dumped without errors, plus Title Update 3. |
| Controller works in the client but not in the game | On **INSTALL**, press **Install controller layer** (or **Reinstall controller layer**). |
| "Can't reach the server" on ONLINE | Check your internet and firewall. The client keeps retrying by itself. |
| Stutter in the first fights | Normal the first time; it goes away. |

Saves and the game's settings are in `%LOCALAPPDATA%\TTT2Recomp`. If the game misbehaves, post in
`#bug-reports` on the Discord with `%LOCALAPPDATA%\TTT2Recomp\log.txt` attached.

Tekken Tag Tournament 2 PC port by shihabal3amri
([github.com/shihabal3amri/tekken-tag-tournament-2-pc](https://github.com/shihabal3amri/tekken-tag-tournament-2-pc)).
This client isn't affiliated with the port's author or with Bandai Namco.
