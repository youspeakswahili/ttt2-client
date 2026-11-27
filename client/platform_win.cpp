// TTT2 Client - Windows implementation of platform.h.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <iphlpapi.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <dwmapi.h>
#include <mmdeviceapi.h>
#include <mmsystem.h>
#include <propsys.h>
#include <SDL3/SDL.h>
#include <filesystem>
#include "platform.h"
#include "../common/input_config.h"

namespace {
std::wstring W(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}
std::string U(const std::wstring& w)
{
    if (w.empty()) return {};
    const int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), int(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}
std::string Env(const wchar_t* name)
{
    wchar_t buf[MAX_PATH * 2];
    const DWORD n = GetEnvironmentVariableW(name, buf, DWORD(std::size(buf)));
    return n && n < std::size(buf) ? U(buf) : std::string();
}
std::string LastError()
{
    const DWORD e = GetLastError();
    wchar_t* msg = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, nullptr, e, 0,
                   reinterpret_cast<wchar_t*>(&msg), 0, nullptr);
    std::string s = msg ? ttt2::Trim(U(msg)) : "error " + std::to_string(e);
    if (msg) LocalFree(msg);
    return s;
}
void MakeDirs(const std::string& dir) { SHCreateDirectoryExW(nullptr, W(dir).c_str(), nullptr); }
} // namespace

namespace plat {

std::string JoinPath(const std::string& a, const std::string& b)
{
    if (a.empty()) return b;
    return a.back() == '\\' || a.back() == '/' ? a + b : a + "\\" + b;
}
std::string ClientDataDir() { const std::string d = JoinPath(Env(L"LOCALAPPDATA"), "TTT2Client"); MakeDirs(d); return d; }
std::string GameDataDir() { return JoinPath(Env(L"LOCALAPPDATA"), "TTT2Recomp"); }
std::string TempDir() { const std::string d = JoinPath(Env(L"TEMP"), "TTT2Client"); MakeDirs(d); return d; }
std::string DefaultInstallDir() { return "C:\\Games\\TTT2"; }
std::string ExeDir()
{
    wchar_t p[MAX_PATH];
    GetModuleFileNameW(nullptr, p, MAX_PATH);
    std::wstring s = p;
    return U(s.substr(0, s.find_last_of(L'\\')));
}

bool FileExists(const std::string& path)
{
    const DWORD a = GetFileAttributesW(W(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && !(a & FILE_ATTRIBUTE_DIRECTORY);
}
bool DirExists(const std::string& path)
{
    const DWORD a = GetFileAttributesW(W(path).c_str());
    return a != INVALID_FILE_ATTRIBUTES && (a & FILE_ATTRIBUTE_DIRECTORY);
}
bool ReadText(const std::string& path, std::string& out)
{
    HANDLE f = CreateFileW(W(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    out.clear();
    char buf[16384];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof buf, &got, nullptr) && got) out.append(buf, got);
    CloseHandle(f);
    if (out.size() >= 3 && out.compare(0, 3, "\xEF\xBB\xBF") == 0) out.erase(0, 3);
    return true;
}
bool WriteText(const std::string& path, const std::string& text)
{
    const size_t slash = path.find_last_of("\\/");
    if (slash != std::string::npos) MakeDirs(path.substr(0, slash));
    const std::wstring tmp = W(path + ".tmp");
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = WriteFile(f, text.data(), DWORD(text.size()), &put, nullptr) && put == text.size();
    CloseHandle(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    return MoveFileExW(tmp.c_str(), W(path).c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH) != 0;
}
bool CopyFileTo(const std::string& from, const std::string& to, std::string& error)
{
    if (CopyFileW(W(from).c_str(), W(to).c_str(), FALSE)) return true;
    error = LastError();
    return false;
}
uint64_t FreeBytes(const std::string& dir)
{
    std::wstring d = W(dir);
    while (!d.empty()) {
        ULARGE_INTEGER free{};
        if (GetDiskFreeSpaceExW(d.c_str(), &free, nullptr, nullptr)) return free.QuadPart;
        const size_t at = d.find_last_of(L'\\');
        if (at == std::wstring::npos) break;
        d.resize(at);
        if (d.size() == 2 && d[1] == L':') d += L'\\';
    }
    return 0;
}

bool ReadBinary(const std::string& path, std::string& out)
{
    HANDLE f = CreateFileW(W(path).c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    out.clear();
    char buf[65536];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof buf, &got, nullptr) && got) out.append(buf, got);
    CloseHandle(f);
    return true;
}
bool WriteBinary(const std::string& path, const std::string& data)
{
    HANDLE f = CreateFileW(W(path).c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD put = 0;
    const bool ok = WriteFile(f, data.data(), DWORD(data.size()), &put, nullptr) && put == data.size() && FlushFileBuffers(f);
    CloseHandle(f);
    return ok;
}
bool MoveFileReplace(const std::string& from, const std::string& to)
{
    return MoveFileExW(W(from).c_str(), W(to).c_str(), MOVEFILE_REPLACE_EXISTING) != 0;
}
bool DeleteFileAt(const std::string& path) { return DeleteFileW(W(path).c_str()) != 0; }
void MakeDir(const std::string& dir) { MakeDirs(dir); }
std::vector<std::string> ListFilesRecursive(const std::string& dir)
{
    std::vector<std::string> out;
    std::vector<std::string> todo{ dir };
    while (!todo.empty()) {
        const std::string d = todo.back();
        todo.pop_back();
        WIN32_FIND_DATAW fd{};
        HANDLE h = FindFirstFileW(W(JoinPath(d, "*")).c_str(), &fd);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            const std::wstring n = fd.cFileName;
            if (n == L"." || n == L"..") continue;
            const std::string full = JoinPath(d, U(n));
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && todo.size() < 64) todo.push_back(full);
            } else {
                out.push_back(full);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    return out;
}
static bool g_second = false;
bool SecondPlayer() { return g_second; }
void UseSecondPlayer()
{
    namespace fs = std::filesystem;
    const std::wstring real = W(Env(L"LOCALAPPDATA"));
    if (real.empty() || g_second) return;
    const fs::path twin = fs::path(real) / L"TTT2 Player 2";
    std::error_code ec;
    if (!fs::exists(twin / L"TTT2Client", ec)) {
        // the first player's settings, binds, install choice and game saves; not the account (the server knows one
        // player by it), not a match in progress, not logs
        auto copyTree = [&](const wchar_t* name, std::initializer_list<const wchar_t*> skip) {
            const fs::path from = fs::path(real) / name, to = twin / name;
            fs::create_directories(to, ec);
            if (!fs::exists(from, ec)) return;
            for (fs::recursive_directory_iterator it(from, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end; it.increment(ec)) {
                const fs::path rel = it->path().lexically_relative(from);
                const std::wstring top = rel.begin()->wstring(), file = it->path().filename().wstring();
                bool skipped = false;
                for (const wchar_t* s : skip) skipped = skipped || top == s || file == s;
                if (skipped) { if (it->is_directory(ec)) it.disable_recursion_pending(); continue; }
                std::error_code e2;
                if (it->is_directory(e2)) fs::create_directories(to / rel, e2);
                else fs::copy_file(it->path(), to / rel, fs::copy_options::overwrite_existing, e2);
            }
            ec.clear();
        };
        copyTree(L"TTT2Recomp", { L"log.txt", L"log-previous.txt", L"log_start.txt", L"flip-log.txt", L"net" });
        copyTree(L"TTT2Client", { L"account.dat", L"match.ini", L"match_prompt.ini", L"match_prompt.ini.answer", L"voice_live.ini" });
        // in a window, so the two games sit next to each other
        WritePrivateProfileStringW(L"Display", L"Mode", L"0", (twin / L"TTT2Recomp" / L"settings.ini").c_str());
    }
    SetEnvironmentVariableW(L"LOCALAPPDATA", twin.c_str());
    _wputenv_s(L"LOCALAPPDATA", twin.c_str());
    g_second = true;
}
bool Relaunch(const std::string& args)
{
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    std::wstring cmd = L"\"" + std::wstring(exe) + L"\" " + W(args);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe, cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, W(ExeDir()).c_str(), &si, &pi)) return false;
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}

bool LooksLikeInstall(const std::string& dir)
{
    return !dir.empty() && FileExists(JoinPath(dir, kGameExe)) && FileExists(JoinPath(dir, "game\\default.xex")) &&
           FileExists(JoinPath(dir, "game\\default.xexp"));
}
std::string RegisteredInstall() { return {}; }
std::string FileVersion(const std::string& exe)
{
    const std::wstring w = W(exe);
    DWORD h = 0;
    const DWORD n = GetFileVersionInfoSizeW(w.c_str(), &h);
    if (!n) return {};
    std::vector<char> data(n);
    if (!GetFileVersionInfoW(w.c_str(), 0, n, data.data())) return {};
    VS_FIXEDFILEINFO* fi = nullptr;
    UINT len = 0;
    if (!VerQueryValueW(data.data(), L"\\", reinterpret_cast<void**>(&fi), &len) || !fi) return {};
    char t[48];
    std::snprintf(t, sizeof t, "%u.%u.%u", HIWORD(fi->dwProductVersionMS), LOWORD(fi->dwProductVersionMS), HIWORD(fi->dwProductVersionLS));
    return t;
}

bool IsGameRunning()
{
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return false;
    PROCESSENTRY32W e{};
    e.dwSize = sizeof e;
    bool found = false;
    const std::wstring name = W(kGameExe);
    for (BOOL ok = Process32FirstW(snap, &e); ok && !found; ok = Process32NextW(snap, &e))
        found = _wcsicmp(e.szExeFile, name.c_str()) == 0;
    CloseHandle(snap);
    return found;
}
bool LaunchGame(const std::string& dir, std::string& error)
{
    std::wstring exe = W(JoinPath(dir, kGameExe));
    std::wstring cmd = L"\"" + exe + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, W(dir).c_str(), &si, &pi)) {
        error = LastError();
        return false;
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return true;
}
uintptr_t LaunchWithEnv(const std::string& dir, const std::string& exeName,
                        const std::vector<std::pair<std::string, std::string>>& env, std::string& error)
{
    // The child inherits the client's environment plus these (set for the moment of the start only).
    std::vector<std::pair<std::wstring, std::wstring>> old;
    for (const auto& [k, v] : env) {
        wchar_t buf[4096];
        const DWORD n = GetEnvironmentVariableW(W(k).c_str(), buf, 4096);
        old.emplace_back(W(k), n && n < 4096 ? std::wstring(buf, n) : std::wstring(L"\x01"));
        SetEnvironmentVariableW(W(k).c_str(), W(v).c_str());
    }
    std::wstring exe = W(JoinPath(dir, exeName));
    std::wstring cmd = L"\"" + exe + L"\"";
    STARTUPINFOW si{};
    si.cb = sizeof si;
    PROCESS_INFORMATION pi{};
    const BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, FALSE, 0, nullptr, W(dir).c_str(), &si, &pi);
    if (!ok) error = LastError();
    for (const auto& [k, v] : old) SetEnvironmentVariableW(k.c_str(), v == L"\x01" ? nullptr : v.c_str());
    if (!ok) return 0;
    CloseHandle(pi.hThread);
    return reinterpret_cast<uintptr_t>(pi.hProcess);
}
bool ProcessDone(uintptr_t handle)
{
    if (!handle) return true;
    HANDLE h = reinterpret_cast<HANDLE>(handle);
    if (WaitForSingleObject(h, 0) != WAIT_OBJECT_0) return false;
    CloseHandle(h);
    return true;
}
int ConnectionKind()
{
    // the interface Windows would use to reach the internet, then its type
    DWORD index = 0;
    IPAddr dest = 0x08080808;   // 8.8.8.8 (no packet is sent)
    if (GetBestInterface(dest, &index) != NO_ERROR) return 0;
    ULONG size = 16 * 1024;
    std::vector<uint8_t> buf(size);
    auto* a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
    ULONG r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, a, &size);
    if (r == ERROR_BUFFER_OVERFLOW) {
        buf.resize(size);
        a = reinterpret_cast<IP_ADAPTER_ADDRESSES*>(buf.data());
        r = GetAdaptersAddresses(AF_INET, GAA_FLAG_SKIP_ANYCAST | GAA_FLAG_SKIP_MULTICAST | GAA_FLAG_SKIP_DNS_SERVER, nullptr, a, &size);
    }
    if (r != NO_ERROR) return 0;
    for (; a; a = a->Next)
        if (a->IfIndex == index) return a->IfType == IF_TYPE_IEEE80211 ? 2 : a->IfType == IF_TYPE_ETHERNET_CSMACD ? 1 : 0;
    return 0;
}

std::string RegionName()
{
    const GEOID geo = GetUserGeoID(GEOCLASS_NATION);
    if (geo == GEOID_NOT_AVAILABLE) return "";
    wchar_t name[128] = {};
    if (!GetGeoInfoW(geo, GEO_FRIENDLYNAME, name, 128, 0)) return "";
    char utf8[256] = {};
    WideCharToMultiByte(CP_UTF8, 0, name, -1, utf8, sizeof utf8, nullptr, nullptr);
    return utf8;
}

void KillProcess(uintptr_t handle)
{
    if (!handle) return;
    HANDLE h = reinterpret_cast<HANDLE>(handle);
    TerminateProcess(h, 0);
    WaitForSingleObject(h, 3000);
    CloseHandle(h);
}
void OpenFolder(const std::string& dir) { ShellExecuteW(nullptr, L"open", W(dir).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
void OpenUrl(const std::string& url) { ShellExecuteW(nullptr, L"open", W(url).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }

int RunElevatedAndWait(const std::string& exe, const std::string& args, std::string& error)
{
    const std::wstring file = W(exe), params = W(args);
    SHELLEXECUTEINFOW sei{};
    sei.cbSize = sizeof sei;
    sei.fMask = SEE_MASK_NOCLOSEPROCESS | SEE_MASK_NOASYNC;
    sei.lpVerb = L"runas";
    sei.lpFile = file.c_str();
    sei.lpParameters = params.c_str();
    sei.nShow = SW_HIDE;
    if (!ShellExecuteExW(&sei) || !sei.hProcess) {
        error = GetLastError() == ERROR_CANCELLED ? "Windows asked for permission and it was declined." : LastError();
        return -1;
    }
    WaitForSingleObject(sei.hProcess, INFINITE);
    DWORD code = 1;
    GetExitCodeProcess(sei.hProcess, &code);
    CloseHandle(sei.hProcess);
    return int(code);
}
int RunHiddenAndWait(const std::string& exe, const std::string& args, const std::string& dir, std::string& error,
                     const std::function<void()>& tick)
{
    std::wstring cmd = L"\"" + W(exe) + L"\" " + W(args);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    PROCESS_INFORMATION pi{};
    if (!CreateProcessW(W(exe).c_str(), cmd.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        dir.empty() ? nullptr : W(dir).c_str(), &si, &pi)) {
        error = LastError();
        return -1;
    }
    CloseHandle(pi.hThread);
    while (WaitForSingleObject(pi.hProcess, 500) == WAIT_TIMEOUT)
        if (tick) tick();
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    return int(code);
}
uint64_t FolderBytes(const std::string& dir)
{
    uint64_t total = 0;
    std::vector<std::wstring> todo{ W(dir) };
    while (!todo.empty()) {
        const std::wstring d = todo.back();
        todo.pop_back();
        WIN32_FIND_DATAW f;
        HANDLE h = FindFirstFileW((d + L"\\*").c_str(), &f);
        if (h == INVALID_HANDLE_VALUE) continue;
        do {
            if (!wcscmp(f.cFileName, L".") || !wcscmp(f.cFileName, L"..")) continue;
            if (f.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) todo.push_back(d + L"\\" + f.cFileName);
            else total += (uint64_t(f.nFileSizeHigh) << 32) | f.nFileSizeLow;
        } while (FindNextFileW(h, &f));
        FindClose(h);
    }
    return total;
}
bool CopyFileElevated(const std::string& from, const std::string& to, std::string& error)
{
    if (CopyFileW(W(from).c_str(), W(to).c_str(), FALSE)) return true;
    if (GetLastError() != ERROR_ACCESS_DENIED) { error = LastError(); return false; }
    // The folder needs admin (Program Files): one elevated copy through cmd.
    const std::string args = "/c copy /y \"" + from + "\" \"" + to + "\"";
    char sys[MAX_PATH];
    GetSystemDirectoryA(sys, MAX_PATH);
    const int rc = RunElevatedAndWait(std::string(sys) + "\\cmd.exe", args, error);
    if (rc != 0 && error.empty()) error = "The copy failed.";
    return rc == 0 && FileExists(to);
}

namespace {
struct Url { std::wstring host, path; INTERNET_PORT port = INTERNET_DEFAULT_HTTPS_PORT; };
bool SplitUrl(const std::string& url, Url& u)
{
    URL_COMPONENTS c{};
    c.dwStructSize = sizeof c;
    wchar_t host[256], path[2048];
    c.lpszHostName = host; c.dwHostNameLength = 256;
    c.lpszUrlPath = path; c.dwUrlPathLength = 2048;
    wchar_t extra[2048]; c.lpszExtraInfo = extra; c.dwExtraInfoLength = 2048;
    const std::wstring w = W(url);
    if (!WinHttpCrackUrl(w.c_str(), 0, 0, &c)) return false;
    u.host.assign(host, c.dwHostNameLength);
    u.path.assign(path, c.dwUrlPathLength);
    u.path.append(extra, c.dwExtraInfoLength);
    u.port = c.nPort;
    return true;
}
bool Request(const std::string& url, const std::function<bool(const char*, DWORD, uint64_t)>& sink, std::string& error)
{
    Url u;
    if (!SplitUrl(url, u)) { error = "Bad address: " + url; return false; }
    HINTERNET s = WinHttpOpen(L"TTT2Client/1.0", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) s = WinHttpOpen(L"TTT2Client/1.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!s) { error = LastError(); return false; }
    HINTERNET c = WinHttpConnect(s, u.host.c_str(), u.port, 0);
    HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", u.path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    bool ok = false;
    if (r && WinHttpSendRequest(r, L"Accept: application/vnd.github+json, */*\r\n", DWORD(-1), WINHTTP_NO_REQUEST_DATA, 0, 0, 0) &&
        WinHttpReceiveResponse(r, nullptr)) {
        DWORD status = 0, len = sizeof status;
        WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &len, nullptr);
        wchar_t cl[32] = {};
        DWORD cll = sizeof cl;
        uint64_t total = 0;
        if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH, nullptr, cl, &cll, nullptr)) total = _wcstoui64(cl, nullptr, 10);
        if (status != 200) {
            error = "The server answered " + std::to_string(status) + ".";
        } else {
            ok = true;
            std::vector<char> buf(1 << 16);
            for (;;) {
                DWORD got = 0;
                if (!WinHttpReadData(r, buf.data(), DWORD(buf.size()), &got)) { error = LastError(); ok = false; break; }
                if (!got) break;
                if (!sink(buf.data(), got, total)) { error = "Writing the download failed."; ok = false; break; }
            }
        }
    } else if (error.empty()) {
        error = "Could not connect: " + LastError();
    }
    if (r) WinHttpCloseHandle(r);
    if (c) WinHttpCloseHandle(c);
    WinHttpCloseHandle(s);
    return ok;
}
} // namespace

bool HttpGet(const std::string& url, std::string& body, std::string& error)
{
    body.clear();
    return Request(url, [&](const char* p, DWORD n, uint64_t) { body.append(p, n); return body.size() < (64u << 20); }, error);
}
bool HttpDownload(const std::string& url, const std::string& toFile, std::string& error, const std::function<void(uint64_t, uint64_t)>& progress)
{
    const std::wstring tmp = W(toFile + ".part");
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) { error = LastError(); return false; }
    uint64_t done = 0;
    const bool ok = Request(url, [&](const char* p, DWORD n, uint64_t total) {
        DWORD put = 0;
        if (!WriteFile(f, p, n, &put, nullptr) || put != n) return false;
        done += n;
        if (progress) progress(done, total);
        return true;
    }, error);
    CloseHandle(f);
    if (!ok) { DeleteFileW(tmp.c_str()); return false; }
    if (!MoveFileExW(tmp.c_str(), W(toFile).c_str(), MOVEFILE_REPLACE_EXISTING)) { error = LastError(); return false; }
    return true;
}

ttt2::SharedPads* SharedPads()
{
    static ttt2::SharedPads* p = [] () -> ttt2::SharedPads* {
        HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(ttt2::SharedPads), ttt2::SharedPadsName());
        if (!m) return nullptr;
        const bool created = GetLastError() != ERROR_ALREADY_EXISTS;
        auto* v = static_cast<ttt2::SharedPads*>(MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(ttt2::SharedPads)));
        if (v && (created || v->magic != ttt2::SharedPads::kMagic)) {
            ZeroMemory(v, sizeof *v);
            v->magic = ttt2::SharedPads::kMagic;
            v->version = 1;
        }
        return v;
    }();
    return p;
}

void StyleWindow(SDL_Window* w)
{
    HWND hwnd = static_cast<HWND>(SDL_GetPointerProperty(SDL_GetWindowProperties(w), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
    if (!hwnd) return;
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, 20 /*DWMWA_USE_IMMERSIVE_DARK_MODE*/, &dark, sizeof dark);
    const COLORREF caption = RGB(10, 10, 13);
    DwmSetWindowAttribute(hwnd, 35 /*DWMWA_CAPTION_COLOR*/, &caption, sizeof caption);
}


void PlayAlert() { PlaySoundW(L"SystemNotification", nullptr, SND_ALIAS | SND_ASYNC | SND_NODEFAULT); }

bool KeyHeld(int vk) { return vk > 0 && vk < 256 && (GetAsyncKeyState(vk) & 0x8000) != 0; }

std::string KeyName(int vk)
{
    UINT sc = MapVirtualKeyW(UINT(vk), MAPVK_VK_TO_VSC_EX);
    if (!sc) return "";
    LONG lp = LONG((sc & 0xFF) << 16);
    if ((sc & 0xFF00) == 0xE000) lp |= 1 << 24;   // extended key
    wchar_t name[64] = {};
    return GetKeyNameTextW(lp, name, 64) > 0 ? U(name) : std::string();
}

namespace {
// PKEY_Device_FriendlyName
const PROPERTYKEY kFriendlyName = { { 0xa45c254e, 0xdf1c, 0x4efd, { 0x80, 0x20, 0x67, 0xd1, 0x46, 0xa8, 0x50, 0xe0 } }, 14 };

struct ComScope {
    bool mine;
    ComScope() { const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED); mine = SUCCEEDED(hr); }
    ~ComScope() { if (mine) CoUninitialize(); }
};
IMMDeviceEnumerator* Enumerator()
{
    IMMDeviceEnumerator* en = nullptr;
    CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator), reinterpret_cast<void**>(&en));
    return en;
}
std::string DeviceId(IMMDevice* d)
{
    LPWSTR id = nullptr;
    std::string r;
    if (SUCCEEDED(d->GetId(&id)) && id) { r = U(id); CoTaskMemFree(id); }
    return r;
}
} // namespace

std::vector<Monitor> Monitors()
{
    std::vector<Monitor> out;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR m, HDC, LPRECT, LPARAM p) -> BOOL {
        auto& list = *reinterpret_cast<std::vector<Monitor>*>(p);
        MONITORINFOEXW mi{};
        mi.cbSize = sizeof(mi);
        if (!GetMonitorInfoW(m, &mi)) return TRUE;
        Monitor mon;
        mon.id = U(mi.szDevice);
        mon.primary = (mi.dwFlags & MONITORINFOF_PRIMARY) != 0;
        // the monitor's own name (EnumDisplayDevices on the adapter's device: "DELL U2719D"), else "Display 2"
        std::string model;
        DISPLAY_DEVICEW dd{};
        dd.cb = sizeof(dd);
        if (EnumDisplayDevicesW(mi.szDevice, 0, &dd, 0) && dd.DeviceString[0]) model = U(dd.DeviceString);
        std::string number = mon.id;
        const size_t at = number.find("DISPLAY");
        number = at == std::string::npos ? number : "Display " + number.substr(at + 7);
        char size[48];
        std::snprintf(size, sizeof size, " - %ld x %ld", long(mi.rcMonitor.right - mi.rcMonitor.left), long(mi.rcMonitor.bottom - mi.rcMonitor.top));
        mon.name = number + size;
        if (!model.empty() && model != "Generic PnP Monitor") mon.name += " (" + model + ")";
        if (mon.primary) mon.name += ", main";
        list.push_back(mon);
        return TRUE;
    }, reinterpret_cast<LPARAM>(&out));
    return out;
}

std::vector<AudioDevice> AudioDevices(bool microphones)
{
    std::vector<AudioDevice> out;
    ComScope com;
    IMMDeviceEnumerator* en = Enumerator();
    if (!en) return out;
    IMMDeviceCollection* list = nullptr;
    if (SUCCEEDED(en->EnumAudioEndpoints(microphones ? eCapture : eRender, DEVICE_STATE_ACTIVE, &list))) {
        UINT n = 0;
        list->GetCount(&n);
        for (UINT i = 0; i < n; ++i) {
            IMMDevice* d = nullptr;
            if (FAILED(list->Item(i, &d))) continue;
            AudioDevice a{ DeviceId(d), "" };
            IPropertyStore* props = nullptr;
            if (SUCCEEDED(d->OpenPropertyStore(STGM_READ, &props))) {
                PROPVARIANT v;
                PropVariantInit(&v);
                if (SUCCEEDED(props->GetValue(kFriendlyName, &v)) && v.vt == VT_LPWSTR && v.pwszVal) a.name = U(v.pwszVal);
                PropVariantClear(&v);
                props->Release();
            }
            if (a.name.empty()) a.name = "Sound device";
            if (!a.id.empty()) out.push_back(a);
            d->Release();
        }
        list->Release();
    }
    en->Release();
    return out;
}

std::string DefaultAudioDevice(bool microphone)
{
    ComScope com;
    IMMDeviceEnumerator* en = Enumerator();
    if (!en) return "";
    IMMDevice* d = nullptr;
    std::string id;
    if (SUCCEEDED(en->GetDefaultAudioEndpoint(microphone ? eCapture : eRender, microphone ? eCommunications : eConsole, &d)) ||
        (microphone && SUCCEEDED(en->GetDefaultAudioEndpoint(eCapture, eConsole, &d)))) {
        id = DeviceId(d);
        d->Release();
    }
    en->Release();
    return id;
}

} // namespace plat
