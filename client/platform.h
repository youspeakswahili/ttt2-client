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

// The installed port
static const char kGameExe[] = "Tekken Tag Tournament 2.exe";
bool LooksLikeInstall(const std::string& dir);   // exe + game\tekken.img
std::string RegisteredInstall();                  // the port installer's InstallLocation, or ""
std::string FileVersion(const std::string& exe);  // VERSIONINFO ProductVersion, or ""

// Processes
bool IsGameRunning();
bool LaunchGame(const std::string& dir, std::string& error);
void OpenFolder(const std::string& dir);
void OpenUrl(const std::string& url);

// Runs exe elevated (Windows asks for admin) and waits; returns the exit code, -1 when it could not start
// (the player said no, or the file is missing). cancelled is polled (the process is left running).
int RunElevatedAndWait(const std::string& exe, const std::string& args, std::string& error);
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
