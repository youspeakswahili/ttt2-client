// The client's version: shown in the client, compared with GitHub's latest release, and written into the exe's
// version info (res/client.rc). Bump the numbers together for every release.
// Alpha builds keep the "-alpha" label; the updater and the server compare only the three numbers, so each new
// alpha must raise one of them (0.5.0-alpha, 0.5.1-alpha, ...), never just the label.
#pragma once
#define TTT2_CLIENT_VERSION "0.5.0-alpha"
#define TTT2_CLIENT_VERSION_COMMAS 0, 5, 0, 0
