// TTT2 Client - Windows implementation of platform.h.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <winhttp.h>
#include <dwmapi.h>
#include <SDL3/SDL.h>
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
std::string DefaultInstallDir() { return "C:\\Games\\Tekken Tag Tournament 2"; }
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

bool LooksLikeInstall(const std::string& dir)
{
    return !dir.empty() && FileExists(JoinPath(dir, kGameExe)) && FileExists(JoinPath(dir, "game\\tekken.img"));
}
std::string RegisteredInstall()
{
    HKEY k = nullptr;
    std::string out;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall\\TTT2Recomp", 0,
                      KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        wchar_t v[MAX_PATH] = {};
        DWORD n = sizeof v - sizeof(wchar_t), type = 0;
        if (RegQueryValueExW(k, L"InstallLocation", nullptr, &type, reinterpret_cast<BYTE*>(v), &n) == ERROR_SUCCESS && type == REG_SZ)
            out = U(v);
        RegCloseKey(k);
    }
    while (!out.empty() && (out.back() == '\\' || out.back() == '/')) out.pop_back();
    return out;
}
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

} // namespace plat
