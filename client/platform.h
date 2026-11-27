// TTT2 Client - what the client needs from the operating system. platform_win.cpp is the real one;
// platform_posix.cpp is a stand-in so the UI can be built and screenshotted off Windows.
#pragma once
#include <cstdint>
#include <functional>
#include <string>
#include <vector>

struct SDL_Window;
namespace ttt2 { struct SharedPads; }

namespace plat {

// Folders (UTF-8)
std::string ClientDataDir();           // %LOCALAPPDATA%\TTT2Client (created)
std::string GameDataDir();             // %LOCALAPPDATA%\TTT2Recomp (the port's saves and settings.ini)
std::string ExeDir();                  // the client's own folder
std::string TempDir();                 // %TEMP%\TTT2Client (created)
std::string DefaultInstallDir();       // C:\Games\Tekken Tag Tournament 2
std::string JoinPath(const std::string& a, const std::string& b);

// Files
bool FileExists(const std::string& path);
bool DirExists(const std::string& path);
bool ReadText(const std::string& path, std::string& out);
bool WriteText(const std::string& path, const std::string& text);   // atomic (temp file + rename)
bool CopyFileTo(const std::string& from, const std::string& to, std::string& error);
uint64_t FreeBytes(const std::string& dir);   // free space on dir's drive (or its nearest existing parent); 0 = unknown
bool ReadBinary(const std::string& path, std::string& out);
bool WriteBinary(const std::string& path, const std::string& data);
bool MoveFileReplace(const std::string& from, const std::string& to);   // rename; works on a running exe
bool DeleteFileAt(const std::string& path);
void MakeDir(const std::string& dir);
std::vector<std::string> ListFilesRecursive(const std::string& dir);
// Starts the client's exe again with these arguments (after an update), without waiting.
bool Relaunch(const std::string& args);
// Solo testing ("--player2"): this copy of the client is a second player on the same PC, with its own account,
// settings, binds and game saves ("TTT2 Player 2" in the local app data; made from the first player's on first use,
// all but the account). Called before anything reads the data folders; the game it starts gets the same folders.
void UseSecondPlayer();
bool SecondPlayer();

// The installed port
static const char kGameExe[] = "ttt2.exe";
bool LooksLikeInstall(const std::string& dir);   // ttt2.exe + game\default.xex + game\default.xexp
std::string RegisteredInstall();                  // always "" (kept for older settings)
std::string FileVersion(const std::string& exe);  // VERSIONINFO ProductVersion, or ""

// Processes
bool IsGameRunning();
bool LaunchGame(const std::string& dir, std::string& error);
// The online build (kOnlineExe in the game folder) started for one match with extra environment variables.
// Returns a process handle (0 = failed); ProcessDone(handle) is true once it exited (and closes the handle).
static const char kOnlineExe[] = "TTT2 Online.exe";
uintptr_t LaunchWithEnv(const std::string& dir, const std::string& exeName,
                        const std::vector<std::pair<std::string, std::string>>& env, std::string& error);
bool ProcessDone(uintptr_t handle);
// This PC's connection to the internet: 0 unknown, 1 wired, 2 Wi-Fi (the adapter of the default route).
int ConnectionKind();
// This PC's region as Windows has it (Settings -> Time & language -> Region), e.g. "Sweden"; "" unknown.
std::string RegionName();
void KillProcess(uintptr_t handle);   // ends it and closes the handle

// Keys, as Windows virtual-key codes: held right now (whichever window is in front), and the keyboard layout's own
// name for one ("" when it has none; app.cpp names the common keys itself).
bool KeyHeld(int vk);
std::string KeyName(int vk);

// A short Windows notification sound (a match was found).
void PlayAlert();

// Sound devices that are plugged in and enabled: the endpoint id (what the game is given) and the name Windows shows.
struct AudioDevice { std::string id, name; };
std::vector<AudioDevice> AudioDevices(bool microphones);
// Windows' default: the communications microphone, or the default output (what the game uses when none is chosen).
std::string DefaultAudioDevice(bool microphone);
// The monitors connected now: Windows' device name (what the game is given, \\.\DISPLAY2) and a name to show.
struct Monitor { std::string id, name; bool primary = false; };
std::vector<Monitor> Monitors();
void OpenFolder(const std::string& dir);
void OpenUrl(const std::string& url);

// Runs exe elevated (Windows asks for admin) and waits; returns the exit code, -1 when it could not start
// (the player said no, or the file is missing). cancelled is polled (the process is left running).
int RunElevatedAndWait(const std::string& exe, const std::string& args, std::string& error);
// Runs exe with no window from dir and waits; tick() is called about twice a second while it runs.
// Returns the exit code, -1 when it could not start.
int RunHiddenAndWait(const std::string& exe, const std::string& args, const std::string& dir, std::string& error,
                     const std::function<void()>& tick);
uint64_t FolderBytes(const std::string& dir);   // total size of the files under dir
// Copies a file into a folder that may need admin: tries directly, then through an elevated copy.
bool CopyFileElevated(const std::string& from, const std::string& to, std::string& error);

// HTTP (HTTPS GET, redirects followed). progress(bytes, total) may be called often; total 0 = unknown.
bool HttpGet(const std::string& url, std::string& body, std::string& error);
bool HttpDownload(const std::string& url, const std::string& toFile, std::string& error,
                  const std::function<void(uint64_t, uint64_t)>& progress);

// The controller layer's shared block (nullptr when the platform has none)
ttt2::SharedPads* SharedPads();

// Window chrome
void StyleWindow(SDL_Window* w);   // dark title bar

} // namespace plat
