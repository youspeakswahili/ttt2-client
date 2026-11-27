// TTT2 Client - stand-in platform for building and screenshotting the UI off Windows. Nothing here
// installs or launches anything; TTT2_FAKE_INSTALL=1 pretends the game is installed.
#include "platform.h"
#include "../common/input_config.h"
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <sys/stat.h>
#include <cstdio>

namespace plat {
std::string JoinPath(const std::string& a, const std::string& b) { return a.empty() ? b : a.back() == '/' ? a + b : a + "/" + b; }
static std::string Base() { const char* h = std::getenv("TTT2_FAKE_HOME"); std::string d = h ? h : "/tmp/ttt2-client-home"; mkdir(d.c_str(), 0755); return d; }
std::string ClientDataDir() { std::string d = JoinPath(Base(), "TTT2Client"); mkdir(d.c_str(), 0755); return d; }
std::string GameDataDir() { std::string d = JoinPath(Base(), "TTT2Recomp"); mkdir(d.c_str(), 0755); return d; }
std::string ExeDir() { return "."; }
std::string TempDir() { return "/tmp"; }
std::string DefaultInstallDir() { return "C:\\Games\\TTT2"; }
bool FileExists(const std::string& p) { struct stat s; return stat(p.c_str(), &s) == 0 && S_ISREG(s.st_mode); }
bool DirExists(const std::string& p) { struct stat s; return stat(p.c_str(), &s) == 0 && S_ISDIR(s.st_mode); }
bool ReadText(const std::string& p, std::string& out) { std::ifstream f(p, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true; }
bool WriteText(const std::string& p, const std::string& t) { std::ofstream f(p, std::ios::binary); f << t; return bool(f); }
bool CopyFileTo(const std::string&, const std::string&, std::string& e) { e = "not on this platform"; return false; }
uint64_t FreeBytes(const std::string&) { return 120ull << 30; }
bool ReadBinary(const std::string& p, std::string& out) { std::ifstream f(p, std::ios::binary); if (!f) return false; std::stringstream ss; ss << f.rdbuf(); out = ss.str(); return true; }
bool WriteBinary(const std::string& p, const std::string& d) { std::ofstream f(p, std::ios::binary); f << d; return bool(f); }
bool MoveFileReplace(const std::string& from, const std::string& to) { return std::rename(from.c_str(), to.c_str()) == 0; }
bool DeleteFileAt(const std::string& p) { return std::remove(p.c_str()) == 0; }
void MakeDir(const std::string& d) { mkdir(d.c_str(), 0755); }
std::vector<std::string> ListFilesRecursive(const std::string&) { return {}; }
bool Relaunch(const std::string&) { return false; }
static bool g_second = false;
bool SecondPlayer() { return g_second; }
void UseSecondPlayer() { if (g_second) return; const std::string d = Base() + "-player2"; setenv("TTT2_FAKE_HOME", d.c_str(), 1); g_second = true; }
bool LooksLikeInstall(const std::string& dir) { return std::getenv("TTT2_FAKE_INSTALL") && !dir.empty(); }
std::string RegisteredInstall() { return std::getenv("TTT2_FAKE_INSTALL") ? "C:\\Games\\TTT2" : ""; }
std::string FileVersion(const std::string&) { return std::getenv("TTT2_FAKE_INSTALL") ? "0.2.0" : ""; }
bool IsGameRunning() { return false; }
bool LaunchGame(const std::string&, std::string& e) { e = "not on this platform"; return false; }
uintptr_t LaunchWithEnv(const std::string&, const std::string&, const std::vector<std::pair<std::string, std::string>>&, std::string& e)
{
    e = "not on this platform";
    return 0;
}
bool ProcessDone(uintptr_t) { return true; }
int ConnectionKind() { const char* e = std::getenv("TTT2_FAKE_CONN"); return e ? std::atoi(e) : 0; }
std::string RegionName() { const char* e = std::getenv("TTT2_FAKE_REGION"); return e ? e : ""; }
void KillProcess(uintptr_t) {}
void PlayAlert() {}
bool KeyHeld(int) { return false; }
std::string KeyName(int) { return ""; }
// Previews: TTT2_FAKE_MICS / TTT2_FAKE_OUTS = "id:name;id:name", the first one is the default
static std::vector<AudioDevice> FakeAudio(bool mic)
{
    std::vector<AudioDevice> v;
    const char* e = std::getenv(mic ? "TTT2_FAKE_MICS" : "TTT2_FAKE_OUTS");
    std::string s = e ? e : "";
    size_t at = 0;
    while (at < s.size()) {
        size_t end = s.find(';', at);
        if (end == std::string::npos) end = s.size();
        const std::string item = s.substr(at, end - at);
        const size_t c = item.find(':');
        if (c != std::string::npos) v.push_back({ item.substr(0, c), item.substr(c + 1) });
        at = end + 1;
    }
    return v;
}
std::vector<AudioDevice> AudioDevices(bool microphones) { return FakeAudio(microphones); }
std::vector<Monitor> Monitors()
{
    return { { "\\\\.\\DISPLAY1", "Display 1 - 2560 x 1440", true }, { "\\\\.\\DISPLAY2", "Display 2 - 1920 x 1080", false } };
}
std::string DefaultAudioDevice(bool microphone) { auto v = FakeAudio(microphone); return v.empty() ? "" : v[0].id; }
void OpenFolder(const std::string&) {}
void OpenUrl(const std::string&) {}
int RunHiddenAndWait(const std::string&, const std::string&, const std::string&, std::string& e, const std::function<void()>&) { e = "not on this platform"; return -1; }
uint64_t FolderBytes(const std::string&) { return 0; }
int RunElevatedAndWait(const std::string&, const std::string&, std::string& e) { e = "not on this platform"; return -1; }
bool CopyFileElevated(const std::string&, const std::string&, std::string& e) { e = "not on this platform"; return false; }
bool HttpGet(const std::string&, std::string&, std::string& e) { e = "not on this platform"; return false; }
bool HttpDownload(const std::string&, const std::string&, std::string& e, const std::function<void(uint64_t, uint64_t)>&) { e = "not on this platform"; return false; }
ttt2::SharedPads* SharedPads() { return nullptr; }
void StyleWindow(SDL_Window*) {}
} // namespace plat
