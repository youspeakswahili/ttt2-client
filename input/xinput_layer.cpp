// TTT2 Client - controller layer. Placed next to "Tekken Tag Tournament 2.exe" as xinput9_1_0.dll, the
// DLL the game reads its controllers from (kernel_xam.cpp: XInputGetState every millisecond). Every
// controller SDL3 knows works: Xbox, PlayStation (DualShock 4, DualSense, PS4/PS5 fight sticks such as
// the Victrix Pro FS), Switch, 8BitDo, hitboxes, generic HID sticks. Each is shown to the game as an
// Xbox 360 controller, through the player's map from the client (common/input_config.h) or, without
// one, SDL's own gamepad layout (south = A, east = B, west = X, north = Y).
//
// Latency: a dedicated thread reads the devices every 0.5 ms (high resolution waitable timer) and
// publishes a finished XINPUT_STATE per player; XInputGetState only copies it (no device I/O on the
// game's thread). If SDL cannot start, every call goes to Windows' own xinput1_4.dll instead.
#define WIN32_LEAN_AND_MEAN
#include "../common/gamecontrollerdb.h"
#include <windows.h>
#include <SDL3/SDL.h>
#include "../common/input_config.h"

#include <atomic>
#include <string>

using namespace ttt2;

namespace {

struct XGamepad { WORD wButtons; BYTE bLeftTrigger, bRightTrigger; SHORT sThumbLX, sThumbLY, sThumbRX, sThumbRY; };
struct XState { DWORD dwPacketNumber; XGamepad Gamepad; };
struct XVibration { WORD wLeftMotorSpeed, wRightMotorSpeed; };
struct XCaps { BYTE Type, SubType; WORD Flags; XGamepad Gamepad; XVibration Vibration; };

constexpr DWORD kNotConnected = 1167;   // ERROR_DEVICE_NOT_CONNECTED

struct Slot {
    SDL_JoystickID id = 0;
    SDL_Joystick* joy = nullptr;
    SDL_Gamepad* pad = nullptr;
    std::string guid, name;
    DeviceMap generic;   // for devices SDL does not know as gamepads
    uint16_t prevDirs = 0;
    int lastH = O_RIGHT, lastV = O_UP;
};

Slot g_slots[4];
SRWLOCK g_lock = SRWLOCK_INIT;
XState g_state[4] = {};
bool g_connected[4] = {};
std::atomic<int> g_mode{ 0 };          // 0 starting, 1 SDL running, 2 forwarding to Windows' XInput
INIT_ONCE g_once = INIT_ONCE_STATIC_INIT;
thread_local bool t_layerThread = false;
InputConfig g_cfg;
FILETIME g_cfgTime = {};
SharedPads* g_shared = nullptr;

// Windows' own XInput, for the fallback (and for SDL, should it ever load this DLL by name).
HMODULE g_sys = nullptr;
DWORD(WINAPI* g_sysGetState)(DWORD, XState*) = nullptr;
DWORD(WINAPI* g_sysSetState)(DWORD, XVibration*) = nullptr;
DWORD(WINAPI* g_sysGetCaps)(DWORD, DWORD, XCaps*) = nullptr;
void LoadSystemXInput()
{
    if (g_sys) return;
    wchar_t path[MAX_PATH];
    const UINT n = GetSystemDirectoryW(path, MAX_PATH);
    if (!n || n > MAX_PATH - 20) return;
    lstrcatW(path, L"\\xinput1_4.dll");
    g_sys = LoadLibraryW(path);
    if (!g_sys) return;
    g_sysGetState = reinterpret_cast<decltype(g_sysGetState)>(GetProcAddress(g_sys, "XInputGetState"));
    g_sysSetState = reinterpret_cast<decltype(g_sysSetState)>(GetProcAddress(g_sys, "XInputSetState"));
    g_sysGetCaps = reinterpret_cast<decltype(g_sysGetCaps)>(GetProcAddress(g_sys, "XInputGetCapabilities"));
}

std::wstring ConfigPath()
{
    wchar_t dir[MAX_PATH];
    const DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", dir, MAX_PATH);
    if (!n || n >= MAX_PATH) return {};
    return std::wstring(dir) + L"\\TTT2Client\\input.ini";
}

void ReloadConfigIfChanged()
{
    static std::wstring path = ConfigPath();
    WIN32_FILE_ATTRIBUTE_DATA a{};
    if (path.empty() || !GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &a)) {
        if (g_cfgTime.dwLowDateTime || g_cfgTime.dwHighDateTime) { g_cfg = {}; g_cfgTime = {}; }
        return;
    }
    if (CompareFileTime(&a.ftLastWriteTime, &g_cfgTime) == 0) return;
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return;   // the client is writing it: next time
    std::string text;
    char buf[4096];
    DWORD got = 0;
    while (ReadFile(f, buf, sizeof buf, &got, nullptr) && got) text.append(buf, got);
    CloseHandle(f);
    ParseConfig(text, g_cfg);
    g_cfgTime = a.ftLastWriteTime;
}

void OpenShared()
{
    HANDLE m = CreateFileMappingW(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(SharedPads), SharedPadsName());
    if (!m) return;
    const bool created = GetLastError() != ERROR_ALREADY_EXISTS;
    g_shared = static_cast<SharedPads*>(MapViewOfFile(m, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(SharedPads)));
    if (g_shared && (created || g_shared->magic != SharedPads::kMagic)) {
        ZeroMemory(g_shared, sizeof(SharedPads));
        g_shared->magic = SharedPads::kMagic;
        g_shared->version = 1;
    }
}

bool SourceOn(SDL_Joystick* j, const Source& s, int deadzone)
{
    const int threshold = 32767 * (deadzone < 10 ? 10 : deadzone) / 100;
    switch (s.kind) {
    case S_BUTTON: return SDL_GetJoystickButton(j, s.index);
    case S_HAT: return (SDL_GetJoystickHat(j, s.index) & s.hatDir) != 0;
    case S_AXIS_POS: return SDL_GetJoystickAxis(j, s.index) > threshold;
    case S_AXIS_NEG: return SDL_GetJoystickAxis(j, s.index) < -threshold;
    default: return false;
    }
}

SHORT Deadzoned(Sint16 v, int deadzone)
{
    const int dz = 32767 * deadzone / 100 / 2;   // the stick's analog deadzone: half the digital threshold
    if (v > -dz && v < dz) return 0;
    return SHORT(v);
}

Sint16 Invert(Sint16 v) { return Sint16(v <= -32767 ? 32767 : -v); }

// Rumble asked for by the game: applied on the layer's thread, which owns the devices.
std::atomic<uint32_t> g_rumble[4];
std::atomic<bool> g_rumbleDirty[4];

XGamepad ReadSlot(Slot& s)
{
    XGamepad g{};
    const DeviceMap* map = g_cfg.Find(s.guid);
    uint16_t b = 0;
    if (map) {
        for (int o = 0; o < O_COUNT; ++o) {
            bool on = false;
            for (const Source& src : map->out[o]) on = on || SourceOn(s.joy, src, g_cfg.deadzone);
            if (!on) continue;
            if (o == O_LT) g.bLeftTrigger = 255;
            else if (o == O_RT) g.bRightTrigger = 255;
            else b |= OutBit(o);
        }
    } else if (s.pad) {
        static const struct { SDL_GamepadButton sdl; int out; } kButtons[] = {
            { SDL_GAMEPAD_BUTTON_DPAD_UP, O_UP }, { SDL_GAMEPAD_BUTTON_DPAD_DOWN, O_DOWN }, { SDL_GAMEPAD_BUTTON_DPAD_LEFT, O_LEFT },
            { SDL_GAMEPAD_BUTTON_DPAD_RIGHT, O_RIGHT }, { SDL_GAMEPAD_BUTTON_START, O_START }, { SDL_GAMEPAD_BUTTON_BACK, O_BACK },
            { SDL_GAMEPAD_BUTTON_LEFT_STICK, O_LS }, { SDL_GAMEPAD_BUTTON_RIGHT_STICK, O_RS }, { SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, O_LB },
            { SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, O_RB }, { SDL_GAMEPAD_BUTTON_SOUTH, O_A }, { SDL_GAMEPAD_BUTTON_EAST, O_B },
            { SDL_GAMEPAD_BUTTON_WEST, O_X }, { SDL_GAMEPAD_BUTTON_NORTH, O_Y },
        };
        for (auto& k : kButtons)
            if (SDL_GetGamepadButton(s.pad, k.sdl)) b |= OutBit(k.out);
        // triggers: analog 0..32767 -> 0..255
        g.bLeftTrigger = BYTE(SDL_GetGamepadAxis(s.pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) >> 7);
        g.bRightTrigger = BYTE(SDL_GetGamepadAxis(s.pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) >> 7);
        const Sint16 lx = SDL_GetGamepadAxis(s.pad, SDL_GAMEPAD_AXIS_LEFTX), ly = SDL_GetGamepadAxis(s.pad, SDL_GAMEPAD_AXIS_LEFTY);
        if (g_cfg.stick == STICK_ANALOG) {
            g.sThumbLX = Deadzoned(lx, g_cfg.deadzone);
            g.sThumbLY = Deadzoned(Invert(ly), g_cfg.deadzone);   // SDL: down +, XInput: up +
        } else if (g_cfg.stick == STICK_DPAD) {
            const int t = 32767 * g_cfg.deadzone / 100;
            if (lx < -t) b |= OutBit(O_LEFT);
            if (lx > t) b |= OutBit(O_RIGHT);
            if (ly < -t) b |= OutBit(O_UP);
            if (ly > t) b |= OutBit(O_DOWN);
        }
        const Sint16 rx = SDL_GetGamepadAxis(s.pad, SDL_GAMEPAD_AXIS_RIGHTX), ry = SDL_GetGamepadAxis(s.pad, SDL_GAMEPAD_AXIS_RIGHTY);
        g.sThumbRX = Deadzoned(rx, g_cfg.deadzone);
        g.sThumbRY = Deadzoned(Invert(ry), g_cfg.deadzone);
    }
    if (!map && !s.pad) {
        // an unknown device with no map yet: the generic layout (input_config.h GenericMap)
        for (int o = 0; o < O_COUNT; ++o) {
            bool on = false;
            for (const Source& src : s.generic.out[o]) on = on || SourceOn(s.joy, src, g_cfg.deadzone);
            if (!on) continue;
            if (o == O_LT) g.bLeftTrigger = 255;
            else if (o == O_RT) g.bRightTrigger = 255;
            else b |= OutBit(o);
        }
    }
    // SOCD on the digital directions, with "last input wins" tracking
    const uint16_t dirs = b & 0x000F, fresh = dirs & ~s.prevDirs;
    if (fresh & OutBit(O_LEFT)) s.lastH = O_LEFT;
    if (fresh & OutBit(O_RIGHT)) s.lastH = O_RIGHT;
    if (fresh & OutBit(O_UP)) s.lastV = O_UP;
    if (fresh & OutBit(O_DOWN)) s.lastV = O_DOWN;
    s.prevDirs = dirs;
    g.wButtons = CleanSocd(b, g_cfg.socdLR, g_cfg.socdUD, s.lastH, s.lastV);
    return g;
}

void AddDevice(SDL_JoystickID id)
{
    for (auto& s : g_slots) if (s.id == id) return;
    for (auto& s : g_slots) {
        if (s.id) continue;
        s = {};
        if (SDL_IsGamepad(id)) {
            s.pad = SDL_OpenGamepad(id);
            s.joy = s.pad ? SDL_GetGamepadJoystick(s.pad) : nullptr;
        }
        if (!s.joy) s.joy = SDL_OpenJoystick(id);
        if (!s.joy) { if (s.pad) SDL_CloseGamepad(s.pad); s = {}; return; }
        s.id = id;
        char guid[64] = {};
        SDL_GUIDToString(SDL_GetJoystickGUID(s.joy), guid, sizeof guid);
        s.guid = guid;
        const char* name = SDL_GetJoystickName(s.joy);
        s.name = name ? name : "Controller";
        s.generic = GenericMap(SDL_GetNumJoystickButtons(s.joy), SDL_GetNumJoystickHats(s.joy));
        return;
    }
}

void RemoveDevice(SDL_JoystickID id)
{
    for (auto& s : g_slots) {
        if (s.id != id) continue;
        if (s.pad) SDL_CloseGamepad(s.pad);
        else if (s.joy) SDL_CloseJoystick(s.joy);
        s = {};
    }
}

DWORD WINAPI LayerThread(void*)
{
    t_layerThread = true;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_ENHANCED_REPORTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_THREAD, "1");
    if (!SDL_Init(SDL_INIT_GAMEPAD)) {
        LoadSystemXInput();
        g_mode = 2;
        return 0;
    }
    SDL_AddGamepadMappingsFromIO(SDL_IOFromConstMem(kGameControllerDb, sizeof kGameControllerDb - 1), true);
    OpenShared();
    ReloadConfigIfChanged();
    g_mode = 1;
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    HANDLE timer = CreateWaitableTimerExW(nullptr, nullptr, 0x00000002 /*CREATE_WAITABLE_TIMER_HIGH_RESOLUTION*/, TIMER_ALL_ACCESS);
    if (!timer) timer = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    uint32_t tick = 0;
    for (;;) {
        SDL_Event e;
        SDL_PumpEvents();
        while (SDL_PollEvent(&e)) {
            if (e.type == SDL_EVENT_JOYSTICK_ADDED) AddDevice(e.jdevice.which);
            else if (e.type == SDL_EVENT_JOYSTICK_REMOVED) RemoveDevice(e.jdevice.which);
        }
        if (++tick % 500 == 0) ReloadConfigIfChanged();   // ~4 times a second

        for (int i = 0; i < 4; ++i)
            if (g_rumbleDirty[i].exchange(false) && g_slots[i].joy) {
                const uint32_t r = g_rumble[i];
                SDL_RumbleJoystick(g_slots[i].joy, Uint16(r >> 16), Uint16(r), r ? 5000 : 0);
            }
        XState next[4] = {};
        bool conn[4] = {};
        for (int i = 0; i < 4; ++i) {
            if (!g_slots[i].id) continue;
            conn[i] = true;
            next[i].Gamepad = ReadSlot(g_slots[i]);
        }
        if (g_shared) {
            uint32_t mask = 0;
            for (int i = 0; i < 4; ++i) {
                const uint16_t inj = g_shared->inject[i];
                if (inj) { next[i].Gamepad.wButtons |= inj; conn[i] = true; }
                if (conn[i]) mask |= 1u << i;
                lstrcpynA(g_shared->names[i], conn[i] && g_slots[i].id ? g_slots[i].name.c_str() : "", 64);
            }
            g_shared->connected = mask;
            g_shared->layerAlive = GetTickCount();
        }
        AcquireSRWLockExclusive(&g_lock);
        for (int i = 0; i < 4; ++i) {
            if (memcmp(&next[i].Gamepad, &g_state[i].Gamepad, sizeof(XGamepad)) != 0) {
                g_state[i].Gamepad = next[i].Gamepad;
                ++g_state[i].dwPacketNumber;
            }
            g_connected[i] = conn[i];
        }
        ReleaseSRWLockExclusive(&g_lock);

        if (timer) {
            LARGE_INTEGER due;
            due.QuadPart = -5000;   // 0.5 ms
            SetWaitableTimer(timer, &due, 0, nullptr, nullptr, FALSE);
            WaitForSingleObject(timer, 2);
        } else {
            Sleep(1);
        }
    }
}

BOOL CALLBACK StartOnce(PINIT_ONCE, PVOID, PVOID*)
{
    HANDLE t = CreateThread(nullptr, 0, LayerThread, nullptr, 0, nullptr);
    if (t) CloseHandle(t);
    else { LoadSystemXInput(); g_mode = 2; }
    return TRUE;
}

void EnsureStarted()
{
    InitOnceExecuteOnce(&g_once, StartOnce, nullptr, nullptr);
    // The very first calls may come before the thread has opened the devices: wait for it (once, briefly)
    // so the game does not see "no controllers" at startup.
    for (int i = 0; i < 200 && g_mode.load() == 0; ++i) Sleep(1);
}

} // namespace

extern "C" {

__declspec(dllexport) DWORD WINAPI XInputGetState(DWORD user, XState* state)
{
    if (t_layerThread) { LoadSystemXInput(); return g_sysGetState ? g_sysGetState(user, state) : kNotConnected; }
    EnsureStarted();
    if (g_mode == 2) return g_sysGetState ? g_sysGetState(user, state) : kNotConnected;
    if (user > 3 || !state) return ERROR_BAD_ARGUMENTS;
    AcquireSRWLockShared(&g_lock);
    const bool on = g_connected[user];
    if (on) *state = g_state[user];
    ReleaseSRWLockShared(&g_lock);
    return on ? ERROR_SUCCESS : kNotConnected;
}

__declspec(dllexport) DWORD WINAPI XInputSetState(DWORD user, XVibration* v)
{
    if (t_layerThread) { LoadSystemXInput(); return g_sysSetState ? g_sysSetState(user, v) : kNotConnected; }
    EnsureStarted();
    if (g_mode == 2) return g_sysSetState ? g_sysSetState(user, v) : kNotConnected;
    if (user > 3 || !v) return ERROR_BAD_ARGUMENTS;
    AcquireSRWLockShared(&g_lock);
    const bool on = g_connected[user];
    ReleaseSRWLockShared(&g_lock);
    if (!on) return kNotConnected;
    g_rumble[user] = uint32_t(v->wLeftMotorSpeed) << 16 | v->wRightMotorSpeed;
    g_rumbleDirty[user] = true;
    return ERROR_SUCCESS;
}

__declspec(dllexport) DWORD WINAPI XInputGetCapabilities(DWORD user, DWORD flags, XCaps* caps)
{
    if (t_layerThread) { LoadSystemXInput(); return g_sysGetCaps ? g_sysGetCaps(user, flags, caps) : kNotConnected; }
    EnsureStarted();
    if (g_mode == 2) return g_sysGetCaps ? g_sysGetCaps(user, flags, caps) : kNotConnected;
    if (user > 3 || !caps) return ERROR_BAD_ARGUMENTS;
    AcquireSRWLockShared(&g_lock);
    const bool on = g_connected[user];
    ReleaseSRWLockShared(&g_lock);
    if (!on) return kNotConnected;
    ZeroMemory(caps, sizeof *caps);
    caps->Type = 1;      // XINPUT_DEVTYPE_GAMEPAD
    caps->SubType = 1;   // XINPUT_DEVSUBTYPE_GAMEPAD
    caps->Gamepad.wButtons = 0xF3FF;
    caps->Gamepad.bLeftTrigger = caps->Gamepad.bRightTrigger = 255;
    caps->Gamepad.sThumbLX = caps->Gamepad.sThumbLY = caps->Gamepad.sThumbRX = caps->Gamepad.sThumbRY = SHORT(0xFFC0);
    caps->Vibration.wLeftMotorSpeed = caps->Vibration.wRightMotorSpeed = 0xFFFF;
    return ERROR_SUCCESS;
}

__declspec(dllexport) DWORD WINAPI XInputGetDSoundAudioDeviceGuids(DWORD user, GUID* render, GUID* capture)
{
    if (user > 3) return ERROR_BAD_ARGUMENTS;
    if (render) ZeroMemory(render, sizeof *render);
    if (capture) ZeroMemory(capture, sizeof *capture);
    return kNotConnected;
}

} // extern "C"

BOOL WINAPI DllMain(HINSTANCE, DWORD reason, LPVOID)
{
    // Nothing here: SDL starts on the first XInput call, outside the loader lock.
    (void)reason;
    return TRUE;
}
