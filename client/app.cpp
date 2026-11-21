// TTT2 Client - the UI. One window, a sidebar and four pages (Play, Controls, Settings, Install), all
// usable with a controller (D-pad / stick to move, A to choose, LB / RB to change page).
#include "app.h"
#include "builder.h"
#include "ini.h"
#include "platform.h"
#include "../common/input_config.h"

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>

using namespace ttt2;

namespace {

// ---------------------------------------------------------------- look
namespace col {
const ImU32 bg = IM_COL32(9, 9, 12, 255);
const ImU32 side = IM_COL32(14, 14, 18, 255);
const ImU32 card = IM_COL32(20, 20, 26, 255);
const ImU32 cardHi = IM_COL32(28, 28, 36, 255);
const ImU32 line = IM_COL32(40, 40, 50, 255);
const ImU32 text = IM_COL32(236, 236, 240, 255);
const ImU32 dim = IM_COL32(140, 140, 156, 255);
const ImU32 faint = IM_COL32(88, 88, 102, 255);
const ImU32 gold = IM_COL32(70, 160, 255, 255);      // the accent: electric blue (the name is historical)
const ImU32 amber = IM_COL32(233, 180, 76, 255);    // needs attention
const ImU32 goldDeep = IM_COL32(30, 90, 200, 255);
const ImU32 red = IM_COL32(224, 48, 60, 255);
const ImU32 green = IM_COL32(64, 214, 128, 255);
const ImU32 blue = IM_COL32(150, 215, 255, 255);
} // namespace col
ImVec4 V(ImU32 c) { return ImGui::ColorConvertU32ToFloat4(c); }
ImU32 Alpha(ImU32 c, float a) { return (c & 0x00FFFFFF) | (ImU32(std::clamp(a, 0.f, 1.f) * 255.f) << 24); }

struct Fonts { ImFont* body = nullptr; ImFont* bold = nullptr; ImFont* display = nullptr; ImFont* heavy = nullptr; } F;
SDL_Texture* g_mark = nullptr;   // the client's mark (res/mark.h), made in App::Init

void Text(ImFont* f, float size, ImU32 c, const char* s)
{
    ImGui::PushFont(f, size);
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextUnformatted(s);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}
void Wrapped(ImU32 c, const char* s)
{
    ImGui::PushStyleColor(ImGuiCol_Text, c);
    ImGui::TextWrapped("%s", s);
    ImGui::PopStyleColor();
}
// A slanted label tag ("READY", "SOON")
void Tag(const char* s, ImU32 c)
{
    ImGui::PushFont(F.bold, 12.f);
    const ImVec2 sz = ImGui::CalcTextSize(s);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float h = sz.y + 6, w = sz.x + 18, k = 5;
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddQuadFilled({ p.x + k, p.y }, { p.x + w, p.y }, { p.x + w - k, p.y + h }, { p.x, p.y + h }, Alpha(c, 0.16f));
    d->AddText({ p.x + 9, p.y + 3 }, c, s);
    ImGui::Dummy({ w, h });
    ImGui::PopFont();
}
// A card: a rounded panel with an optional heading; EndCard() closes it.
void BeginCard(const char* id, float height = 0, const char* heading = nullptr)
{
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(col::card));
    ImGui::PushStyleColor(ImGuiCol_Border, V(col::line));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 22, 18 });
    ImGui::BeginChild(id, { 0, height }, ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding | (height == 0 ? ImGuiChildFlags_AutoResizeY : 0));
    if (heading) {
        Text(F.display, 22.f, col::text, heading);
        ImGui::Dummy({ 0, 4 });
    }
}
void EndCard()
{
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
}
// The big gold action button (slanted ends)
bool BigButton(const char* label, ImVec2 size, bool enabled = true, ImU32 color = col::gold)
{
    ImGui::PushID(label);
    const ImVec2 p = ImGui::GetCursorScreenPos();
    if (!enabled) ImGui::BeginDisabled();
    const bool pressed = ImGui::InvisibleButton("##b", size);
    if (!enabled) ImGui::EndDisabled();
    const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const float k = size.y * 0.32f;
    const ImU32 fill = !enabled ? col::cardHi : hot ? color : Alpha(color, 0.88f);
    d->AddQuadFilled({ p.x + k, p.y }, { p.x + size.x, p.y }, { p.x + size.x - k, p.y + size.y }, { p.x, p.y + size.y }, fill);
    if (hot && enabled) d->AddQuad({ p.x + k - 3, p.y - 3 }, { p.x + size.x + 3, p.y - 3 }, { p.x + size.x - k + 3, p.y + size.y + 3 }, { p.x - 3, p.y + size.y + 3 }, Alpha(color, 0.5f), 2.f);
    ImGui::PushFont(F.heavy, size.y * 0.5f);
    const ImVec2 ts = ImGui::CalcTextSize(label);
    d->AddText({ p.x + (size.x - ts.x) * 0.5f, p.y + (size.y - ts.y) * 0.5f }, enabled ? IM_COL32(6, 12, 24, 255) : col::faint, label);
    ImGui::PopFont();
    ImGui::PopID();
    return pressed && enabled;
}
// A quiet secondary button
bool Button(const char* label, ImVec2 size = { 0, 0 }, bool enabled = true)
{
    if (!enabled) ImGui::BeginDisabled();
    ImGui::PushStyleColor(ImGuiCol_Button, V(col::cardHi));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(IM_COL32(44, 44, 56, 255)));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, V(col::goldDeep));
    ImGui::PushFont(F.bold, 14.f);
    const bool r = ImGui::Button(label, size);
    ImGui::PopFont();
    ImGui::PopStyleColor(3);
    if (!enabled) ImGui::EndDisabled();
    return r && enabled;
}
// Segmented choice ("Removed | Original"); returns true when it changed
bool Segmented(const char* id, int* value, const char* const* labels, int count, float width = 0)
{
    ImGui::PushID(id);
    bool changed = false;
    const float w = width > 0 ? width : ImGui::GetContentRegionAvail().x;
    const float each = (w - 4.f * float(count - 1)) / float(count);
    for (int i = 0; i < count; ++i) {
        if (i) ImGui::SameLine(0, 4);
        const bool on = *value == i;
        ImGui::PushStyleColor(ImGuiCol_Button, V(on ? col::gold : col::cardHi));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, V(on ? col::gold : IM_COL32(44, 44, 56, 255)));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, V(col::goldDeep));
        ImGui::PushStyleColor(ImGuiCol_Text, V(on ? IM_COL32(6, 12, 24, 255) : col::text));
        ImGui::PushFont(F.bold, 13.5f);
        if (ImGui::Button(labels[i], { each, 34 }) && !on) { *value = i; changed = true; }
        ImGui::PopFont();
        ImGui::PopStyleColor(4);
    }
    ImGui::PopID();
    return changed;
}
// A setting row: label + help on the left, the control on the right
void RowLabel(const char* label, const char* help, float controlWidth)
{
    const float avail = ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup();
    ImGui::PushTextWrapPos(ImGui::GetCursorPosX() + avail - controlWidth - 24);
    Text(F.bold, 15.f, col::text, label);
    if (help) { ImGui::PushFont(F.body, 13.f); Wrapped(col::dim, help); ImGui::PopFont(); }
    ImGui::PopTextWrapPos();
    ImGui::EndGroup();
    ImGui::SameLine(avail - controlWidth + ImGui::GetStyle().WindowPadding.x);
}
void Divider()
{
    ImGui::Dummy({ 0, 6 });
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImGui::GetWindowDrawList()->AddLine(p, { p.x + ImGui::GetContentRegionAvail().x, p.y }, col::line);
    ImGui::Dummy({ 0, 8 });
}
void Check(bool ok, const char* label, const char* detail)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 c = { p.x + 10, p.y + 11 };
    d->AddCircleFilled(c, 10, Alpha(ok ? col::green : col::amber, 0.15f));
    if (ok) {
        d->AddLine({ c.x - 4.5f, c.y }, { c.x - 1, c.y + 3.5f }, col::green, 2.2f);
        d->AddLine({ c.x - 1, c.y + 3.5f }, { c.x + 5, c.y - 3.5f }, col::green, 2.2f);
    } else {
        d->AddLine({ c.x, c.y - 5 }, { c.x, c.y + 1.5f }, col::amber, 2.2f);
        d->AddCircleFilled({ c.x, c.y + 5 }, 1.4f, col::amber);
    }
    ImGui::Dummy({ 30, 22 });
    ImGui::SameLine();
    ImGui::BeginGroup();
    Text(F.bold, 14.5f, col::text, label);
    if (detail && *detail) { ImGui::PushFont(F.body, 13.f); Wrapped(col::dim, detail); ImGui::PopFont(); }
    ImGui::EndGroup();
    ImGui::Dummy({ 0, 4 });
}

// The project's donation page (opened in the browser; the client never handles payments). Empty = no button.
constexpr const char* kSupportUrl = "https://ko-fi.com/mumbaibai";

// ---------------------------------------------------------------- controllers (the client's own view)
struct Device {
    SDL_JoystickID id = 0;
    SDL_Joystick* joy = nullptr;
    SDL_Gamepad* pad = nullptr;
    std::string guid, name;
    DeviceMap generic;   // the layout the game uses for it until it is mapped (no SDL gamepad)
};

// What each output is called on the Controls page (Tekken meaning of the game's default layout)
struct OutInfo { int out; const char* name; const char* role; bool wizard; };
const OutInfo kRows[] = {
    { O_UP, "Up", "Jump / sidestep", true }, { O_DOWN, "Down", "Crouch / sidestep", true },
    { O_LEFT, "Left", "Back", true }, { O_RIGHT, "Right", "Forward", true },
    { O_X, "1", "Left punch", true }, { O_Y, "2", "Right punch", true }, { O_A, "3", "Left kick", true }, { O_B, "4", "Right kick", true },
    { O_LB, "Tag", "Tag (LB in the game)", true },
    { O_M12, "1+2", "Macro", true }, { O_M34, "3+4", "Macro", true }, { O_M13, "1+3", "Macro (throw)", false },
    { O_M24, "2+4", "Macro (throw)", false }, { O_M1234, "1+2+3+4", "Macro", false },
    { O_M134, "1+3+4", "Macro (taunt)", false },
    { O_START, "Start", "Pause / menus", true }, { O_BACK, "Back", "Back / select", true },
    { O_RB, "RB", "Set in the game", false }, { O_RT, "RT", "Set in the game", false }, { O_LT, "LT", "Set in the game", false },
    { O_LS, "L3", "Left stick click", false }, { O_RS, "R3", "Right stick click", false },
};

std::string SourceName(const Source& s)
{
    char t[40] = {};
    switch (s.kind) {
    case S_BUTTON: std::snprintf(t, sizeof t, "Button %u", s.index + 1); break;
    case S_HAT: std::snprintf(t, sizeof t, "Hat %s", s.hatDir == 1 ? "up" : s.hatDir == 2 ? "right" : s.hatDir == 4 ? "down" : "left"); break;
    case S_AXIS_POS: std::snprintf(t, sizeof t, "Axis %u +", s.index + 1); break;
    case S_AXIS_NEG: std::snprintf(t, sizeof t, "Axis %u -", s.index + 1); break;
    default: break;
    }
    return t;
}

// SDL's gamepad layout as the default sources of each output (shown for gamepads without a custom map)
const char* DefaultBinding(int out)
{
    switch (out) {
    case O_UP: return "D-pad / stick up"; case O_DOWN: return "D-pad / stick down"; case O_LEFT: return "D-pad / stick left"; case O_RIGHT: return "D-pad / stick right";
    case O_A: return "South (Cross / A)"; case O_B: return "East (Circle / B)"; case O_X: return "West (Square / X)"; case O_Y: return "North (Triangle / Y)";
    case O_LB: return "L1 / LB"; case O_RB: return "R1 / RB"; case O_LT: return "L2 / LT"; case O_RT: return "R2 / RT";
    case O_START: return "Options / Start"; case O_BACK: return "Share / Back"; case O_LS: return "L3"; case O_RS: return "R3";
    default: return "Not set";
    }
}

bool SourceHeld(SDL_Joystick* j, const Source& s, int deadzone)
{
    const int threshold = 32767 * std::max(deadzone, 10) / 100;
    switch (s.kind) {
    case S_BUTTON: return SDL_GetJoystickButton(j, s.index);
    case S_HAT: return (SDL_GetJoystickHat(j, s.index) & s.hatDir) != 0;
    case S_AXIS_POS: return SDL_GetJoystickAxis(j, s.index) > threshold;
    case S_AXIS_NEG: return SDL_GetJoystickAxis(j, s.index) < -threshold;
    default: return false;
    }
}

} // namespace

// ---------------------------------------------------------------- the app
struct App::Impl {
    SDL_Window* window = nullptr;
    enum Page { PLAY, CONTROLS, SETTINGS, INSTALL, PAGE_COUNT } page = PLAY;

    // client settings
    IniFile client;
    bool fullscreen = false;
    void SetFullscreen(bool on)
    {
        fullscreen = on;
        SDL_SetWindowFullscreen(window, on);   // borderless, at the desktop's resolution
        client.Set("client", "fullscreen", on ? "1" : "0");
        SaveClient();
    }
    std::string installDir;
    bool installed = false, layerInstalled = false, gameRunning = false;
    std::string installedVersion;
    double lastProbe = -10;

    // controllers
    std::vector<Device> devices;
    std::string selectedGuid;
    InputConfig input;
    // capture ("press a button for 1")
    int captureOut = -1;
    bool captureAppend = false, captureWaitRelease = true, wizard = false;
    int wizardIndex = 0;
    double captureStart = 0;
    std::vector<Sint16> baseline;

    // game settings (the port's settings.ini)
    IniFile game;
    bool gameLoaded = false;
    std::string settingsNote;
    double settingsNoteAt = -10;

    // install page
    Builder builder;
    std::string iso, tu, dlc, dest;
    std::mutex dialogLock;
    std::string* dialogTarget = nullptr;
    std::string dialogResult;
    bool dialogDone = false;

    void Load()
    {
        std::string t;
        if (plat::ReadText(plat::JoinPath(plat::ClientDataDir(), "client.ini"), t)) client.Parse(t);
        installDir = client.Get("client", "install_dir");
        iso = client.Get("client", "iso");
        tu = client.Get("client", "tu");
        dlc = client.Get("client", "dlc");
        fullscreen = client.GetInt("client", "fullscreen", 0) != 0;
        if (fullscreen) SDL_SetWindowFullscreen(window, true);
        if (installDir.empty() || !plat::LooksLikeInstall(installDir)) {
            const std::string reg = plat::RegisteredInstall();
            if (plat::LooksLikeInstall(reg)) installDir = reg;
        }
        dest = installDir.empty() ? plat::DefaultInstallDir() : installDir;
        if (plat::ReadText(plat::JoinPath(plat::ClientDataDir(), "input.ini"), t)) ParseConfig(t, input);
        LoadGameSettings();
        Probe();
    }
    void SaveClient()
    {
        client.Set("client", "install_dir", installDir);
        client.Set("client", "iso", iso);
        client.Set("client", "tu", tu);
        client.Set("client", "dlc", dlc);
        plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "client.ini"), client.Text());
    }
    void SaveInput() { plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "input.ini"), WriteConfig(input)); }
    void LoadGameSettings()
    {
        std::string t;
        game = {};
        if (plat::ReadText(plat::JoinPath(plat::GameDataDir(), "settings.ini"), t)) game.Parse(t);
        gameLoaded = true;
    }
    void SaveGameSettings()
    {
        const bool ok = plat::WriteText(plat::JoinPath(plat::GameDataDir(), "settings.ini"), game.Text());
        settingsNote = !ok ? "Could not save the game's settings." : gameRunning ? "Saved. Restart the game to use them." : "Saved.";
        settingsNoteAt = SDL_GetTicks() / 1000.0;
    }
    void Probe()
    {
        installed = plat::LooksLikeInstall(installDir);
        layerInstalled = installed && (plat::FileExists(plat::JoinPath(installDir, "xinput9_1_0.dll")) || !std::getenv("TTT2_FAKE_NO_LAYER"));
#ifdef _WIN32
        layerInstalled = installed && plat::FileExists(plat::JoinPath(installDir, "xinput9_1_0.dll"));
#endif
        installedVersion = installed ? plat::FileVersion(plat::JoinPath(installDir, plat::kGameExe)) : "";
        gameRunning = plat::IsGameRunning();
    }

    // -------------------------------------------------- devices
    // While the game runs, the client lets go of the controllers (unless the Controls page is open), so
    // only one program at a time talks to a PlayStation pad's or stick's HID reports.
    bool devicesSuspended = false;
    void SyncDeviceHold()
    {
        const bool suspend = gameRunning && page != CONTROLS;
        if (suspend == devicesSuspended) return;
        devicesSuspended = suspend;
        if (suspend) {
            StopCaptureIfAny();
            for (auto& d : devices) { if (d.pad) SDL_CloseGamepad(d.pad); else SDL_CloseJoystick(d.joy); }
            devices.clear();
        } else {
            int n = 0;
            if (SDL_JoystickID* ids = SDL_GetJoysticks(&n)) { for (int i = 0; i < n; ++i) AddDevice(ids[i]); SDL_free(ids); }
        }
    }
    void StopCaptureIfAny() { if (captureOut >= 0) StopCapture(); }
    void AddDevice(SDL_JoystickID id)
    {
        if (devicesSuspended) return;
        for (auto& d : devices) if (d.id == id) return;
        Device d;
        d.id = id;
        if (SDL_IsGamepad(id)) { d.pad = SDL_OpenGamepad(id); d.joy = d.pad ? SDL_GetGamepadJoystick(d.pad) : nullptr; }
        if (!d.joy) d.joy = SDL_OpenJoystick(id);
        if (!d.joy) return;
        char g[64] = {};
        SDL_GUIDToString(SDL_GetJoystickGUID(d.joy), g, sizeof g);
        d.guid = g;
        const char* n = SDL_GetJoystickName(d.joy);
        d.name = n ? n : "Controller";
        d.generic = GenericMap(SDL_GetNumJoystickButtons(d.joy), SDL_GetNumJoystickHats(d.joy));
        devices.push_back(d);
        if (selectedGuid.empty()) selectedGuid = d.guid;
    }
    void RemoveDevice(SDL_JoystickID id)
    {
        for (size_t i = 0; i < devices.size(); ++i) {
            if (devices[i].id != id) continue;
            if (devices[i].pad) SDL_CloseGamepad(devices[i].pad); else SDL_CloseJoystick(devices[i].joy);
            if (captureOut >= 0 && devices[i].guid == selectedGuid) StopCapture();
            devices.erase(devices.begin() + long(i));
            break;
        }
        bool still = false;
        for (auto& d : devices) still = still || d.guid == selectedGuid;
        if (!still) selectedGuid = devices.empty() ? "" : devices[0].guid;
    }
    Device* Selected()
    {
        for (auto& d : devices) if (d.guid == selectedGuid) return &d;
        return nullptr;
    }

    // -------------------------------------------------- capture
    void StartCapture(int out, bool append)
    {
        Device* d = Selected();
        if (!d) return;
        captureOut = out;
        captureAppend = append;
        captureWaitRelease = true;
        captureStart = SDL_GetTicks() / 1000.0;
        ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;   // the pad is being listened to
    }
    void StopCapture()
    {
        captureOut = -1;
        wizard = false;
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
    }
    bool AnyHeld(SDL_Joystick* j)
    {
        for (int i = 0; i < SDL_GetNumJoystickButtons(j); ++i) if (SDL_GetJoystickButton(j, i)) return true;
        for (int i = 0; i < SDL_GetNumJoystickHats(j); ++i) if (SDL_GetJoystickHat(j, i)) return true;
        for (int i = 0; i < SDL_GetNumJoystickAxes(j) && i < int(baseline.size()); ++i)
            if (std::abs(int(SDL_GetJoystickAxis(j, i)) - int(baseline[size_t(i)])) > 16000) return true;
        return false;
    }
    void UpdateCapture()
    {
        if (captureOut < 0) return;
        Device* d = Selected();
        const double now = SDL_GetTicks() / 1000.0;
        if (!d || now - captureStart > 10.0 || ImGui::IsKeyPressed(ImGuiKey_Escape)) { StopCapture(); return; }
        SDL_Joystick* j = d->joy;
        if (captureWaitRelease) {
            // everything let go (the press that started the capture included): take the resting axes
            baseline.assign(size_t(SDL_GetNumJoystickAxes(j)), 0);
            for (int i = 0; i < int(baseline.size()); ++i) baseline[size_t(i)] = SDL_GetJoystickAxis(j, i);
            bool held = false;
            for (int i = 0; i < SDL_GetNumJoystickButtons(j); ++i) held = held || SDL_GetJoystickButton(j, i);
            for (int i = 0; i < SDL_GetNumJoystickHats(j); ++i) held = held || SDL_GetJoystickHat(j, i);
            if (!held) captureWaitRelease = false;
            return;
        }
        Source s;
        for (int i = 0; i < SDL_GetNumJoystickButtons(j) && s.kind == S_NONE; ++i)
            if (SDL_GetJoystickButton(j, i)) { s.kind = S_BUTTON; s.index = uint8_t(i); }
        for (int i = 0; i < SDL_GetNumJoystickHats(j) && s.kind == S_NONE; ++i) {
            const Uint8 h = SDL_GetJoystickHat(j, i);
            const Uint8 dir = (h & SDL_HAT_UP) ? 1 : (h & SDL_HAT_DOWN) ? 4 : (h & SDL_HAT_LEFT) ? 8 : (h & SDL_HAT_RIGHT) ? 2 : 0;
            if (dir) { s.kind = S_HAT; s.index = uint8_t(i); s.hatDir = dir; }
        }
        for (int i = 0; i < SDL_GetNumJoystickAxes(j) && i < int(baseline.size()) && s.kind == S_NONE; ++i) {
            const int v = SDL_GetJoystickAxis(j, i), delta = v - baseline[size_t(i)];
            if (std::abs(delta) > 20000) { s.kind = delta > 0 ? S_AXIS_POS : S_AXIS_NEG; s.index = uint8_t(i); }
        }
        if (s.kind == S_NONE) return;
        DeviceMap& m = input.Get(d->guid);
        m.name = d->name;
        if (m.empty()) {   // keep the rest of the layout the game was using
            if (d->pad) SeedFromGamepad(*d, m);
            else for (int o = 0; o < O_COUNT; ++o) m.out[o] = d->generic.out[o];
        }
        auto& list = m.out[captureOut];
        if (!captureAppend) list.clear();
        if (std::find(list.begin(), list.end(), s) == list.end()) list.push_back(s);
        SaveInput();
        while (wizard && ++wizardIndex < int(std::size(kRows)) && !kRows[wizardIndex].wizard) {}
        if (wizard && wizardIndex < int(std::size(kRows))) {   // the wizard walks the rows marked for it
            captureOut = kRows[wizardIndex].out;
            captureAppend = false;
            captureWaitRelease = true;
            captureStart = now;
            return;
        }
        StopCapture();
    }
    // A gamepad's SDL layout written out as raw sources, so one changed button keeps the others
    void SeedFromGamepad(const Device& d, DeviceMap& m)
    {
        int count = 0;
        SDL_GamepadBinding** binds = SDL_GetGamepadBindings(d.pad, &count);
        if (!binds) return;
        auto outOfButton = [](SDL_GamepadButton b) -> int {
            switch (b) {
            case SDL_GAMEPAD_BUTTON_SOUTH: return O_A; case SDL_GAMEPAD_BUTTON_EAST: return O_B; case SDL_GAMEPAD_BUTTON_WEST: return O_X;
            case SDL_GAMEPAD_BUTTON_NORTH: return O_Y; case SDL_GAMEPAD_BUTTON_BACK: return O_BACK; case SDL_GAMEPAD_BUTTON_START: return O_START;
            case SDL_GAMEPAD_BUTTON_LEFT_STICK: return O_LS; case SDL_GAMEPAD_BUTTON_RIGHT_STICK: return O_RS;
            case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return O_LB; case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return O_RB;
            case SDL_GAMEPAD_BUTTON_DPAD_UP: return O_UP; case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return O_DOWN;
            case SDL_GAMEPAD_BUTTON_DPAD_LEFT: return O_LEFT; case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: return O_RIGHT;
            default: return -1;
            }
        };
        for (int i = 0; i < count; ++i) {
            const SDL_GamepadBinding* b = binds[i];
            int out = -1;
            if (b->output_type == SDL_GAMEPAD_BINDTYPE_BUTTON) out = outOfButton(b->output.button);
            else if (b->output_type == SDL_GAMEPAD_BINDTYPE_AXIS) {
                if (b->output.axis.axis == SDL_GAMEPAD_AXIS_LEFT_TRIGGER) out = O_LT;
                else if (b->output.axis.axis == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) out = O_RT;
            }
            if (out < 0) continue;
            Source s;
            if (b->input_type == SDL_GAMEPAD_BINDTYPE_BUTTON) { s.kind = S_BUTTON; s.index = uint8_t(b->input.button); }
            else if (b->input_type == SDL_GAMEPAD_BINDTYPE_HAT) { s.kind = S_HAT; s.index = uint8_t(b->input.hat.hat); s.hatDir = uint8_t(b->input.hat.hat_mask); }
            else if (b->input_type == SDL_GAMEPAD_BINDTYPE_AXIS) {
                s.index = uint8_t(b->input.axis.axis);
                s.kind = b->input.axis.axis_max >= b->input.axis.axis_min ? S_AXIS_POS : S_AXIS_NEG;
            }
            if (s.kind != S_NONE) m.out[out].push_back(s);
        }
        SDL_free(binds);
    }
    bool OutHeld(const Device& d, int out)
    {
        const DeviceMap* m = input.Find(d.guid);
        if (!m && !d.pad) m = &d.generic;
        if (m) {
            for (auto& s : m->out[out]) if (SourceHeld(d.joy, s, input.deadzone)) return true;
            return false;
        }
        static const SDL_GamepadButton map[O_COUNT] = {
            SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT, SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
            SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_BACK, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
            SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST,
            SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH, SDL_GAMEPAD_BUTTON_INVALID, SDL_GAMEPAD_BUTTON_INVALID };
        if (out >= O_M12) return false;   // macros only exist in a custom map
        if (out == O_LT) return SDL_GetGamepadAxis(d.pad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER) > 8000;
        if (out == O_RT) return SDL_GetGamepadAxis(d.pad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER) > 8000;
        bool on = SDL_GetGamepadButton(d.pad, map[out]);
        if (input.stick != STICK_OFF) {
            const int t = 32767 * input.deadzone / 100;
            const Sint16 x = SDL_GetGamepadAxis(d.pad, SDL_GAMEPAD_AXIS_LEFTX), y = SDL_GetGamepadAxis(d.pad, SDL_GAMEPAD_AXIS_LEFTY);
            on = on || (out == O_LEFT && x < -t) || (out == O_RIGHT && x > t) || (out == O_UP && y < -t) || (out == O_DOWN && y > t);
        }
        return on;
    }

    // -------------------------------------------------- dialogs (SDL's file pickers answer on any thread)
    void Pick(std::string* target, bool folder, bool iso)
    {
        {
            std::lock_guard l(dialogLock);
            dialogTarget = target;
            dialogDone = false;
        }
        auto cb = [](void* ud, const char* const* files, int) {
            auto* self = static_cast<Impl*>(ud);
            std::lock_guard l(self->dialogLock);
            self->dialogResult = files && files[0] ? files[0] : "";
            self->dialogDone = true;
        };
        static const SDL_DialogFileFilter isoFilter[] = { { "Xbox 360 disc image", "iso" }, { "All files", "*" } };
        static const SDL_DialogFileFilter anyFilter[] = { { "All files", "*" } };
        if (folder) SDL_ShowOpenFolderDialog(cb, this, window, target->empty() ? nullptr : target->c_str(), false);
        else SDL_ShowOpenFileDialog(cb, this, window, iso ? isoFilter : anyFilter, iso ? 2 : 1, target->empty() ? nullptr : target->c_str(), false);
    }
    void PollDialogs()
    {
        std::lock_guard l(dialogLock);
        if (!dialogDone) return;
        dialogDone = false;
        if (dialogTarget && !dialogResult.empty()) { *dialogTarget = dialogResult; SaveClient(); }
        dialogTarget = nullptr;
    }

    // -------------------------------------------------- frame
    void Frame();
    void Sidebar(float w, float h);
    void Header(const char* title, const char* subtitle);
    void PagePlay();
    void PageControls();
    void PageSettings();
    void PageInstall();
    void Visualizer(Device* d, float height);
};

void App::Impl::Sidebar(float w, float h)
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    d->AddRectFilled(o, { o.x + w, o.y + h }, col::side);
    d->AddLine({ o.x + w, o.y }, { o.x + w, o.y + h }, col::line);
    // the mark (a fist in lightning) and the client's name
    const ImVec2 m = { o.x + 18, o.y + 18 };
    if (g_mark) d->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(g_mark)), m, { m.x + 64, m.y + 64 });
    ImGui::PushFont(F.heavy, 30.f);
    d->AddText({ m.x + 74, m.y + 6 }, col::text, "TTT2 CLIENT");
    ImGui::PopFont();
    ImGui::PushFont(F.bold, 11.5f);
    d->AddText({ m.x + 76, m.y + 40 }, col::gold, "FOR TEKKEN TAG 2");
    ImGui::PopFont();

    ImGui::SetCursorPos({ 0, 104 });
    static const char* names[] = { "PLAY", "CONTROLS", "SETTINGS", "INSTALL" };
    for (int i = 0; i < PAGE_COUNT; ++i) {
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 size = { w - 1, 50 };
        if (ImGui::InvisibleButton("nav", size)) { page = Page(i); if (i == SETTINGS) LoadGameSettings(); }
        const bool active = page == i, hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        if (active) {
            d->AddRectFilledMultiColor(p, { p.x + size.x, p.y + size.y }, Alpha(col::gold, 0.16f), Alpha(col::gold, 0.0f), Alpha(col::gold, 0.0f), Alpha(col::gold, 0.16f));
            d->AddRectFilled(p, { p.x + 4, p.y + size.y }, col::gold);
        } else if (hot) {
            d->AddRectFilled(p, { p.x + size.x, p.y + size.y }, Alpha(col::text, 0.04f));
        }
        ImGui::PushFont(F.display, 24.f);
        d->AddText({ p.x + 30, p.y + 11 }, active ? col::text : hot ? col::text : col::dim, names[i]);
        ImGui::PopFont();
        if (i == INSTALL && !installed) d->AddCircleFilled({ p.x + w - 30, p.y + 25 }, 4.5f, col::amber);
        ImGui::PopID();
    }

    // tip jar (opens the Ko-fi page in the browser)
    if (kSupportUrl[0]) {
        ImGui::SetCursorPos({ 20, h - 214 });
        const ImVec2 sp = ImGui::GetCursorScreenPos();
        const ImVec2 ss = { w - 40, 42 };
        if (ImGui::InvisibleButton("support", ss)) plat::OpenUrl(kSupportUrl);
        const bool shot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        d->AddRectFilled(sp, { sp.x + ss.x, sp.y + ss.y }, shot ? Alpha(col::red, 0.22f) : Alpha(col::red, 0.10f), 8.f);
        d->AddRect(sp, { sp.x + ss.x, sp.y + ss.y }, Alpha(col::red, shot ? 0.8f : 0.4f), 8.f);
        // a small heart
        const ImVec2 hc = { sp.x + 22, sp.y + 22 };
        d->AddCircleFilled({ hc.x - 3.2f, hc.y - 2.5f }, 4.2f, col::red);
        d->AddCircleFilled({ hc.x + 3.2f, hc.y - 2.5f }, 4.2f, col::red);
        d->AddTriangleFilled({ hc.x - 7.2f, hc.y - 1.0f }, { hc.x + 7.2f, hc.y - 1.0f }, { hc.x, hc.y + 7.5f }, col::red);
        ImGui::PushFont(F.bold, 14.f);
        d->AddText({ sp.x + 40, sp.y + 12 }, col::text, "Gift me a Kinder Maxi");
        ImGui::PopFont();
    }

    // bottom: status
    ImGui::SetCursorPos({ 24, h - 150 });
    ImGui::BeginGroup();
    Text(F.bold, 11.5f, col::faint, "GAME");
    Text(F.bold, 14.f, installed ? col::text : col::amber, installed ? (gameRunning ? "Running" : "Installed") : "Not installed");
    ImGui::Dummy({ 0, 6 });
    Text(F.bold, 11.5f, col::faint, "CONTROLLERS");
    char t[64];
    std::snprintf(t, sizeof t, devicesSuspended ? "In use by the game" : devices.empty() ? "None connected" : "%zu connected", devices.size());
    Text(F.bold, 14.f, devices.empty() && !devicesSuspended ? col::dim : col::text, t);
    ImGui::Dummy({ 0, 6 });
    Text(F.body, 11.5f, col::faint, "LB / RB  change page");
    ImGui::EndGroup();
}

void App::Impl::Header(const char* title, const char* subtitle)
{
    Text(F.heavy, 46.f, col::text, title);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6);
    Text(F.body, 15.f, col::dim, subtitle);
    ImGui::Dummy({ 0, 14 });
}

void App::Impl::PagePlay()
{
    // the hero: a banner with slashes in the game's gold and red
    const float bw = ImGui::GetContentRegionAvail().x, bh = 250;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, { p.x + bw, p.y + bh }, col::card, 12.f);
    d->PushClipRect(p, { p.x + bw, p.y + bh }, true);
    for (int i = 0; i < 7; ++i) {
        const float x = p.x + bw * 0.52f + float(i) * 58.f, k = 120.f;
        const ImU32 c = i % 3 == 1 ? Alpha(col::blue, 0.07f + 0.015f * float(i)) : Alpha(col::gold, 0.05f + 0.015f * float(i));
        d->AddQuadFilled({ x + k, p.y }, { x + k + 26, p.y }, { x + 26, p.y + bh }, { x, p.y + bh }, c);
    }
    d->AddRectFilledMultiColor(p, { p.x + bw * 0.7f, p.y + bh }, col::card, Alpha(col::card, 0.f), Alpha(col::card, 0.f), col::card);
    d->PopClipRect();
    d->AddRect(p, { p.x + bw, p.y + bh }, col::line, 12.f);

    ImGui::SetCursorScreenPos({ p.x + 34, p.y + 30 });
    ImGui::BeginGroup();
    if (installed) Tag(gameRunning ? "IN GAME" : "READY", gameRunning ? col::blue : col::green);
    else Tag("SETUP NEEDED", col::amber);
    ImGui::Dummy({ 0, 2 });
    Text(F.heavy, 64.f, col::text, "FIGHT");
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 10);
    std::string sub = installed ? "Version " + (installedVersion.empty() ? std::string("unknown") : installedVersion) + "  ·  " + installDir
                                : "Build the game from your own disc image to start.";
    Text(F.body, 14.f, col::dim, sub.c_str());
    ImGui::Dummy({ 0, 18 });
    if (installed) {
        if (BigButton(gameRunning ? "RUNNING" : "LAUNCH GAME", { 260, 56 }, !gameRunning)) {
            std::string err;
            if (!plat::LaunchGame(installDir, err)) { settingsNote = "Could not start the game: " + err; settingsNoteAt = SDL_GetTicks() / 1000.0; }
            else gameRunning = true;
        }
    } else if (BigButton("SET UP THE GAME", { 300, 56 })) {
        page = INSTALL;
    }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos({ p.x, p.y + bh + 18 });

    // ready check + online
    const float half = (ImGui::GetContentRegionAvail().x - 18) * 0.5f;
    ImGui::BeginGroup();
    ImGui::PushID("ready");
    ImGui::BeginChild("readycol", { half, 0 }, ImGuiChildFlags_AutoResizeY);
    BeginCard("readycard", 0, "Ready check");
    Check(installed, "Game installed", installed ? "Built from your disc image." : "Go to Install.");
    Check(layerInstalled, "Every controller supported", layerInstalled ? "The controller layer is in the game's folder." : "Install it from the Install page.");
    const bool delayOff = game.GetInt("Game", "InputDelay", 0) == 0;
    Check(delayOff, "Offline input delay removed", delayOff ? "The original game's 2 extra frames are gone." : "Turned back on in Settings.");
    const int ll = game.GetInt("Display", "LowLatencyMode", 2);
    Check(ll != 2, "Low latency", ll == 0 ? "Auto: one frame faster whenever your PC keeps 60 fps." : ll == 1 ? "On in matches." : "Off. Settings → Auto saves a frame.");
    std::string pads;
    for (auto& dv : devices) pads += (pads.empty() ? "" : ", ") + dv.name;
    Check(!devices.empty(), devices.empty() ? "No controller connected" : "Controllers connected", pads.empty() ? "Plug one in; the keyboard works too." : pads.c_str());
    EndCard();
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::EndGroup();
    ImGui::SameLine(0, 18);
    ImGui::BeginChild("onlinecol", { half, 0 }, ImGuiChildFlags_AutoResizeY);
    BeginCard("onlinecard", 0, "Online");
    ImGui::PushFont(F.body, 14.f);
    Wrapped(col::dim, "Ranked and player matches run inside the game for now, with the port's rollback netcode over Steam. "
                      "Keep Steam running and open ONLINE MODE in the game.");
    ImGui::PopFont();
    ImGui::Dummy({ 0, 8 });
    Text(F.bold, 12.f, col::faint, "NEXT IN THE CLIENT");
    ImGui::Dummy({ 0, 2 });
    for (const char* s : { "Ranked and casual queues from here", "Practice while you wait for a match", "Leaderboards and match history" }) {
        ImGui::Bullet();
        ImGui::SameLine();
        Text(F.body, 14.f, col::text, s);
    }
    EndCard();
    ImGui::EndChild();
}

void App::Impl::Visualizer(Device* dv, float height)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, { p.x + w, p.y + height }, IM_COL32(13, 13, 17, 255), 10.f);
    auto held = [&](int out) { return dv && OutHeld(*dv, out); };
    // the lever
    const ImVec2 c = { p.x + w * 0.2f, p.y + height * 0.5f };
    const float r = height * 0.3f;
    d->AddCircle(c, r, col::line, 48, 2.f);
    float dx = 0, dy = 0;
    if (held(O_LEFT)) dx -= 1;
    if (held(O_RIGHT)) dx += 1;
    if (held(O_UP)) dy -= 1;
    if (held(O_DOWN)) dy += 1;
    const bool moving = dx || dy;
    const float len = moving ? std::sqrt(dx * dx + dy * dy) : 1.f;
    const ImVec2 ball = { c.x + dx / len * r * 0.62f, c.y + dy / len * r * 0.62f };
    d->AddLine(c, ball, col::faint, 6.f);
    d->AddCircleFilled(ball, r * 0.34f, moving ? col::red : IM_COL32(60, 60, 72, 255));
    // eight buttons, Vewlix layout: 1 2 RB LB over 3 4 RT LT (the right 60% of the panel)
    struct B { int out; const char* label; float x, y; };
    const float areaX = p.x + w * 0.36f, areaW = w * 0.56f;
    const float s = std::min(areaW / 4.4f, height * 0.36f);
    const float bx = areaX + (areaW - s * 4.25f) * 0.5f, by = p.y + height * 0.5f;
    const B buttons[] = {
        { O_X, "1", 0, -0.55f }, { O_Y, "2", 1, -0.72f }, { O_RB, "RB", 2, -0.72f }, { O_LB, "TAG", 3, -0.62f },
        { O_A, "3", -0.12f, 0.52f }, { O_B, "4", 0.88f, 0.35f }, { O_RT, "RT", 1.88f, 0.35f }, { O_LT, "LT", 2.88f, 0.45f },
    };
    for (auto& b : buttons) {
        const ImVec2 bc = { bx + b.x * s * 1.08f + s * 0.5f, by + b.y * s };
        const bool on = held(b.out);
        const bool face = b.out == O_X || b.out == O_Y || b.out == O_A || b.out == O_B;
        d->AddCircleFilled(bc, s * 0.42f, on ? (face ? col::gold : col::red) : IM_COL32(32, 32, 40, 255), 40);
        d->AddCircle(bc, s * 0.42f, on ? col::text : col::line, 40, 1.5f);
        ImGui::PushFont(face ? F.heavy : F.bold, face ? s * 0.42f : s * 0.24f);
        const ImVec2 ts = ImGui::CalcTextSize(b.label);
        d->AddText({ bc.x - ts.x * 0.5f, bc.y - ts.y * 0.5f }, on ? IM_COL32(6, 12, 24, 255) : col::dim, b.label);
        ImGui::PopFont();
    }
    // back / start, bottom right
    for (int i = 0; i < 2; ++i) {
        const int out = i ? O_START : O_BACK;
        const ImVec2 sc = { p.x + w - 70 + float(i) * 40, p.y + height - 34 };
        d->AddRectFilled({ sc.x - 14, sc.y - 6 }, { sc.x + 14, sc.y + 6 }, held(out) ? col::gold : IM_COL32(32, 32, 40, 255), 6.f);
    }
    ImGui::PushFont(F.bold, 10.5f);
    d->AddText({ p.x + w - 90, p.y + height - 24 }, col::faint, "BACK   START");
    ImGui::PopFont();
    if (!dv) {
        ImGui::PushFont(F.bold, 14.f);
        const char* msg = "Connect a controller";
        const ImVec2 ts = ImGui::CalcTextSize(msg);
        d->AddRectFilled({ p.x, p.y }, { p.x + w, p.y + height }, Alpha(col::bg, 0.6f), 10.f);
        d->AddText({ p.x + (w - ts.x) * 0.5f, p.y + (height - ts.y) * 0.5f }, col::dim, msg);
        ImGui::PopFont();
    }
    ImGui::Dummy({ w, height });
}

void App::Impl::PageControls()
{
    Header("CONTROLS", "Every controller works: pads, fight sticks and hitboxes. Map any button here; the game picks it up at once.");
    UpdateCapture();
    Device* dv = Selected();

    const float left = 300;
    ImGui::BeginChild("devs", { left, 0 }, ImGuiChildFlags_AutoResizeY);
    BeginCard("devcard", 0, "Controllers");
    if (devices.empty()) {
        ImGui::PushFont(F.body, 14.f);
        Wrapped(col::dim, "Plug in a controller or stick. PlayStation and fight-stick models work as they are, no extra tools.");
        ImGui::PopFont();
    }
    for (auto& dvc : devices) {
        ImGui::PushID(dvc.guid.c_str());
        const bool sel = dvc.guid == selectedGuid;
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float w = ImGui::GetContentRegionAvail().x;
        if (ImGui::InvisibleButton("dev", { w, 54 }) && captureOut < 0) selectedGuid = dvc.guid;
        const bool hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        ImDrawList* d = ImGui::GetWindowDrawList();
        d->AddRectFilled(p, { p.x + w, p.y + 54 }, sel ? Alpha(col::gold, 0.12f) : hot ? col::cardHi : 0, 8.f);
        if (sel) d->AddRect(p, { p.x + w, p.y + 54 }, Alpha(col::gold, 0.6f), 8.f);
        ImGui::PushFont(F.bold, 14.f);
        d->AddText({ p.x + 12, p.y + 9 }, col::text, dvc.name.c_str());
        ImGui::PopFont();
        ImGui::PushFont(F.body, 12.f);
        const bool custom = input.Find(dvc.guid) != nullptr;
        d->AddText({ p.x + 12, p.y + 30 }, custom ? col::gold : col::dim, custom ? "Custom map" : dvc.pad ? "Gamepad layout" : "Generic layout");
        ImGui::PopFont();
        ImGui::Dummy({ 0, 4 });
        ImGui::PopID();
    }
    EndCard();
    ImGui::Dummy({ 0, 12 });
    // Hitbox / stick extras, folded away: most players never need them
    BeginCard("optcard", 0, nullptr);
    ImGui::PushFont(F.display, 20.f);
    ImGui::PushStyleColor(ImGuiCol_Header, V(0));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, V(Alpha(col::text, 0.05f)));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, V(Alpha(col::text, 0.08f)));
    const bool open = ImGui::CollapsingHeader("Advanced");
    ImGui::PopStyleColor(3);
    ImGui::PopFont();
    if (!open) {
        ImGui::PushFont(F.body, 12.5f);
        Wrapped(col::faint, "Hitbox and analog stick options.");
        ImGui::PopFont();
    } else {
    static const char* lr[] = { "Neutral", "Last", "Off" };
    static const char* ud[] = { "Up", "Neutral", "Last", "Off" };
    static const char* st[] = { "Analog", "As D-pad", "Off" };
    ImGui::PushFont(F.body, 12.5f);
    Wrapped(col::faint, "For hitboxes: what happens when opposite directions are held together.");
    ImGui::PopFont();
    Text(F.bold, 13.f, col::dim, "Left + Right held");
    if (Segmented("lr", &input.socdLR, lr, 3)) SaveInput();
    ImGui::Dummy({ 0, 4 });
    Text(F.bold, 13.f, col::dim, "Up + Down held");
    if (Segmented("ud", &input.socdUD, ud, 4)) SaveInput();
    ImGui::Dummy({ 0, 4 });
    Text(F.bold, 13.f, col::dim, "Gamepad left stick");
    if (Segmented("st", &input.stick, st, 3)) SaveInput();
    ImGui::Dummy({ 0, 4 });
    Text(F.bold, 13.f, col::dim, "Stick deadzone");
    ImGui::SetNextItemWidth(-1);
    if (ImGui::SliderInt("##dz", &input.deadzone, 5, 80, "%d%%")) SaveInput();
    }
    EndCard();
    ImGui::EndChild();

    ImGui::SameLine(0, 18);
    ImGui::BeginChild("map", { 0, 0 }, ImGuiChildFlags_AutoResizeY);
    BeginCard("vizcard");
    {
        Text(F.display, 22.f, col::text, dv ? dv->name.c_str() : "Input test");
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 250 + ImGui::GetStyle().WindowPadding.x);
        if (Button("Map all buttons", { 130, 30 }, dv != nullptr)) { wizard = true; wizardIndex = 0; StartCapture(kRows[0].out, false); }
        ImGui::SameLine();
        const bool custom = dv && input.Find(dv->guid);
        if (Button("Reset", { 110, 30 }, custom)) {
            for (auto it = input.devices.begin(); it != input.devices.end(); ++it)
                if (it->guid == dv->guid) { input.devices.erase(it); break; }
            SaveInput();
        }
    }
    ImGui::Dummy({ 0, 6 });
    Visualizer(dv, 170);
    EndCard();
    ImGui::Dummy({ 0, 12 });

    BeginCard("rowscard", 0, nullptr);
    const DeviceMap* custom = dv ? input.Find(dv->guid) : nullptr;
    const DeviceMap* m = custom ? custom : dv && !dv->pad ? &dv->generic : nullptr;
    if (ImGui::BeginTable("rows", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_PadOuterX)) {
        ImGui::TableSetupColumn("out", ImGuiTableColumnFlags_WidthFixed, 70);
        ImGui::TableSetupColumn("role", ImGuiTableColumnFlags_WidthStretch, 1.0f);
        ImGui::TableSetupColumn("src", ImGuiTableColumnFlags_WidthStretch, 1.2f);
        ImGui::TableSetupColumn("act", ImGuiTableColumnFlags_WidthFixed, 170);
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, V(0));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, V(Alpha(col::text, 0.025f)));
        for (const OutInfo& row : kRows) {
            ImGui::PushID(row.out);
            ImGui::TableNextRow(0, 40);
            const bool on = dv && OutHeld(*dv, row.out);
            const bool capturing = captureOut == row.out;
            if (on || capturing) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, capturing ? Alpha(col::red, 0.16f) : Alpha(col::gold, 0.14f));
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            Text(F.heavy, 22.f, on ? col::gold : col::text, row.name);
            ImGui::TableNextColumn();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
            Text(F.body, 13.5f, col::dim, row.role);
            ImGui::TableNextColumn();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
            if (capturing) {
                Text(F.bold, 13.5f, col::red, captureWaitRelease ? "Let go of everything..." : wizard ? "Press it now  (Esc skips the rest)" : "Press it now  (Esc cancels)");
            } else if (m) {
                std::string s;
                for (auto& src : m->out[row.out]) s += (s.empty() ? "" : "  +  ") + SourceName(src);
                Text(custom ? F.bold : F.body, 13.5f, s.empty() ? col::faint : col::text, s.empty() ? "Not set" : s.c_str());
            } else {
                Text(F.body, 13.5f, dv ? col::text : col::faint, dv ? DefaultBinding(row.out) : "-");
            }
            ImGui::TableNextColumn();
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 5);
            if (Button("Set", { 52, 28 }, dv && captureOut < 0)) StartCapture(row.out, false);
            ImGui::SameLine(0, 4);
            if (Button("+ Add", { 58, 28 }, dv && captureOut < 0)) StartCapture(row.out, true);
            ImGui::SameLine(0, 4);
            const bool canClear = custom && !custom->out[row.out].empty();
            if (Button("Clear", { 52, 28 }, canClear && captureOut < 0)) { input.Get(dv->guid).out[row.out].clear(); SaveInput(); }
            ImGui::PopID();
        }
        ImGui::PopStyleColor(2);
        ImGui::EndTable();
    }
    ImGui::Dummy({ 0, 6 });
    ImGui::PushFont(F.body, 13.f);
    Wrapped(col::faint, "Your map is saved by the client and used every time the game starts. For it to mean what it says, keep the "
                        "game's own BUTTON SETTINGS on its defaults (1 2 3 4 on X Y A B, Tag on LB).");
    ImGui::PopFont();
    EndCard();
    ImGui::EndChild();
}

void App::Impl::PageSettings()
{
    Header("SETTINGS", "The game's own settings, in one place. They are read when the game starts.");
    if (!gameLoaded) LoadGameSettings();
    const float ctl = 380;
    auto seg = [&](const char* sec, const char* key, int def, const char* const* labels, int n, const int* values = nullptr) {
        int cur = game.GetInt(sec, key, def), idx = 0;
        if (values) { for (int i = 0; i < n; ++i) if (values[i] == cur) idx = i; }
        else idx = std::clamp(cur, 0, n - 1);
        ImGui::PushID(key);
        if (Segmented(key, &idx, labels, n, ctl)) { game.SetInt(sec, key, values ? values[idx] : idx); SaveGameSettings(); }
        ImGui::PopID();
    };

    BeginCard("clientwin", 0, "This client");
    {
        static const char* fs[] = { "Window", "Full screen" };
        int v = fullscreen ? 1 : 0;
        RowLabel("Client window", "F11 or Alt+Enter switches too. The game's own window mode is under Display below.", ctl);
        ImGui::PushID("clientfs");
        if (Segmented("fs", &v, fs, 2, ctl)) SetFullscreen(v == 1);
        ImGui::PopID();
    }
    EndCard();
    ImGui::Dummy({ 0, 12 });

    BeginCard("latency", 0, "Input and latency");
    {
        static const char* l[] = { "Removed", "Original" };
        RowLabel("Offline input delay", "The original game holds every press for 2 extra frames offline. Removed: about 33 ms faster.", ctl);
        seg("Game", "InputDelay", 0, l, 2);
        Divider();
        static const char* ll[] = { "Auto", "On", "Off" };
        RowLabel("Low latency", "Shows each picture a frame sooner in matches (about 40 → 27 ms). Auto turns it off when your PC can't hold 60 fps.", ctl);
        seg("Display", "LowLatencyMode", 2, ll, 3);
        Divider();
        static const char* vs[] = { "On", "Off" };
        static const int vsv[] = { 1, 0 };
        RowLabel("VSync", "Off can lower delay further but may tear the picture.", ctl);
        seg("Display", "VSync", 1, vs, 2, vsv);
        ImGui::Dummy({ 0, 8 });
        if (Button("Lowest input lag", { 170, 32 })) {
            game.SetInt("Game", "InputDelay", 0);
            game.SetInt("Display", "LowLatencyMode", 0);
            game.SetInt("Online", "Netcode", 1);
            SaveGameSettings();
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        Text(F.body, 13.f, col::faint, "Delay removed, low latency Auto, netcode Prioritize Response.");
    }
    EndCard();
    ImGui::Dummy({ 0, 12 });

    BeginCard("online", 0, "Online netcode");
    {
        static const char* n[] = { "Standard", "Response", "Graphics", "Custom" };
        RowLabel("Rollback balance", "Response: one frame less delay, more rollback. Graphics: one more frame, fewer corrections. "
                                     "The host's choice decides the match.", ctl);
        seg("Online", "Netcode", 0, n, 4);
        if (game.GetInt("Online", "Netcode", 0) == 3) {
            Divider();
            RowLabel("Input delay (frames)", "Both players always share this delay.", ctl);
            int v = std::clamp(game.GetInt("Online", "CustomDelay", 1), 1, 6);
            ImGui::SetNextItemWidth(ctl);
            if (ImGui::SliderInt("##cd", &v, 1, 6)) { game.SetInt("Online", "CustomDelay", v); SaveGameSettings(); }
            Divider();
            RowLabel("Rollback limit (frames)", "The most frames a correction may rewind.", ctl);
            int r = std::clamp(game.GetInt("Online", "CustomRollback", 8), 1, 8);
            ImGui::SetNextItemWidth(ctl);
            if (ImGui::SliderInt("##cr", &r, 1, 8)) { game.SetInt("Online", "CustomRollback", r); SaveGameSettings(); }
        }
        Divider();
        static const char* rm[] = { "On", "Off" };
        static const int rmv[] = { 1, 0 };
        RowLabel("Rematch menu", "REMATCH / CHARACTER SELECT straight after an online match.", ctl);
        seg("Online", "RematchMenu", 1, rm, 2, rmv);
    }
    EndCard();
    ImGui::Dummy({ 0, 12 });

    BeginCard("display", 0, "Display");
    {
        static const char* modes[] = { "Window", "Borderless", "Full screen" };
        RowLabel("Game window mode", "Full screen or borderless for the game itself.", ctl);
        seg("Display", "Mode", 0, modes, 3);
        Divider();
        static const char* res[] = { "640x360", "960x540", "1280x720", "1600x900", "1920x1080", "2560x1440", "3840x2160" };
        RowLabel("Resolution", nullptr, ctl);
        const std::string cur = game.Get("Display", "Resolution", "1280x720");
        ImGui::SetNextItemWidth(ctl);
        ImGui::PushFont(F.bold, 14.f);
        if (ImGui::BeginCombo("##res", cur.c_str())) {
            for (const char* r : res)
                if (ImGui::Selectable(r, cur == r)) { game.Set("Display", "Resolution", r); SaveGameSettings(); }
            ImGui::EndCombo();
        }
        ImGui::PopFont();
        Divider();
        static const char* aa[] = { "Off", "SMAA", "SSAA 1.5x", "SSAA 2x" };
        RowLabel("Anti-aliasing", "Supersampling costs a lot of GPU time.", ctl);
        seg("Display", "AntiAliasing", 0, aa, 4);
        Divider();
        static const char* fps[] = { "Off", "Top left", "Top right" };
        RowLabel("FPS counter", nullptr, ctl);
        seg("Display", "FpsCounter", 0, fps, 3);
        Divider();
        static const char* qs[] = { "Off", "On" };
        RowLabel("Quick start", "Skip the logos and intro movie.", ctl);
        seg("Game", "QuickStart", 0, qs, 2);
    }
    EndCard();
}

void App::Impl::PageInstall()
{
    Header("INSTALL", "Build the PC version from your own Xbox 360 disc. The client downloads the official port and adds the controller layer.");
    builder.Poll();
    PollDialogs();
    const bool busy = builder.Running();

    if (installed) {
        BeginCard("current", 0, "Installed");
        Text(F.bold, 15.f, col::text, installDir.c_str());
        std::string v = "Port version " + (installedVersion.empty() ? std::string("unknown") : installedVersion);
        const std::string latest = builder.LatestTag();
        if (!latest.empty()) v += "  ·  latest " + latest;
        Text(F.body, 13.5f, col::dim, v.c_str());
        if (!builder.CheckError().empty()) Text(F.body, 13.f, col::red, builder.CheckError().c_str());
        ImGui::Dummy({ 0, 6 });
        if (Button(layerInstalled ? "Reinstall controller layer" : "Install controller layer", { 220, 32 }, !busy && !gameRunning)) {
            BuildRequest r;
            r.dest = installDir;
            r.onlyLayer = true;
            builder.Start(r);
        }
        ImGui::SameLine();
        if (Button(builder.Checking() ? "Checking..." : "Check for updates", { 170, 32 }, !builder.Checking())) builder.CheckLatest();
        ImGui::SameLine();
        if (Button("Open folder", { 130, 32 })) plat::OpenFolder(installDir);
        if (gameRunning) { ImGui::Dummy({ 0, 2 }); Text(F.body, 13.f, col::amber, "Close the game to change its files."); }
        EndCard();
        ImGui::Dummy({ 0, 12 });
    }

    BeginCard("build", 0, installed ? "Rebuild or update" : "Build the game");
    ImGui::PushFont(F.body, 14.f);
    Wrapped(col::dim, "You need your own Tekken Tag Tournament 2 (USA, Europe) Xbox 360 disc image and its Title Update 3. "
                      "No game files come with the client or the port; everything is built on this PC. About 13 GB.");
    ImGui::PopFont();
    ImGui::Dummy({ 0, 10 });
    auto field = [&](const char* label, const char* help, std::string& value, const char* id, bool folder, bool isoFilter, bool alsoFolder) {
        ImGui::PushID(id);
        Text(F.bold, 14.f, col::text, label);
        if (help) Text(F.body, 12.5f, col::faint, help);
        const float bw = alsoFolder ? 196 : 96;
        ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - bw - 8);
        char buf[1024];
        std::snprintf(buf, sizeof buf, "%s", value.c_str());
        ImGui::PushFont(F.body, 14.f);
        if (ImGui::InputText("##v", buf, sizeof buf)) { value = buf; SaveClient(); }
        ImGui::PopFont();
        ImGui::SameLine(0, 8);
        if (Button(folder ? "Folder..." : "Browse...", { 96, 0 }, !busy)) Pick(&value, folder, isoFilter);
        if (alsoFolder) { ImGui::SameLine(0, 4); if (Button("Folder...", { 96, 0 }, !busy)) Pick(&value, true, false); }
        ImGui::Dummy({ 0, 8 });
        ImGui::PopID();
    };
    field("Disc image (.iso)", nullptr, iso, "iso", false, true, false);
    field("Title Update 3", "The file TU_174Q22P_0000008000000.00000000000O2, or the folder it is in.", tu, "tu", false, false, true);
    field("Install folder", nullptr, dest, "dest", true, false, false);
    field("DLC folder (optional)", "Your downloaded DLC packages, if you have them.", dlc, "dlc", true, false, false);
    const uint64_t free = plat::FreeBytes(dest);
    if (free) {
        char t[96];
        std::snprintf(t, sizeof t, "%.0f GB free on that drive", double(free) / double(1ull << 30));
        Text(F.body, 12.5f, free < (14ull << 30) ? col::red : col::faint, t);
    }
    ImGui::Dummy({ 0, 8 });
    const bool ready = !iso.empty() && !tu.empty() && !dest.empty() && !busy && !gameRunning;
    if (BigButton(busy ? "BUILDING..." : installed ? "REBUILD" : "BUILD GAME", { 260, 52 }, ready)) {
        BuildRequest r{ iso, tu, dest, dlc, false };
        SaveClient();
        builder.Start(r);
    }
    EndCard();

    if (busy || builder.Finished() || !builder.Log().empty()) {
        ImGui::Dummy({ 0, 12 });
        BeginCard("progress", 0, nullptr);
        Text(F.display, 22.f, col::text, busy ? "Building" : builder.Succeeded() ? "Finished" : "Stopped");
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, V(builder.Finished() && !builder.Succeeded() ? col::red : col::gold));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, V(col::cardHi));
        ImGui::ProgressBar(builder.Progress(), { -1, 8 }, "");
        ImGui::PopStyleColor(2);
        ImGui::Dummy({ 0, 4 });
        Text(F.bold, 14.f, col::text, builder.Status().c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg, V(IM_COL32(12, 12, 15, 255)));
        ImGui::BeginChild("log", { 0, 160 }, ImGuiChildFlags_Borders);
        ImGui::PushFont(F.body, 12.5f);
        for (auto& l : builder.Log()) Wrapped(col::dim, l.c_str());
        if (busy) ImGui::SetScrollHereY(1.0f);
        ImGui::PopFont();
        ImGui::EndChild();
        ImGui::PopStyleColor();
        EndCard();
    }
    if (builder.Finished()) {
        builder.Acknowledge();
        if (builder.Succeeded()) {
            installDir = dest;
            if (!builder.InstalledTag().empty()) client.Set("client", "installed_tag", builder.InstalledTag());
            SaveClient();
            Probe();
        }
    }
    ImGui::Dummy({ 0, 10 });
    ImGui::PushFont(F.body, 12.5f);
    Wrapped(col::faint, "Tekken Tag Tournament 2 PC port by shihabal3amri (github.com/shihabal3amri/tekken-tag-tournament-2-pc). "
                        "This client is not affiliated with the port's author or with Bandai Namco.");
    ImGui::PopFont();
}

void App::Impl::Frame()
{
    const double now = SDL_GetTicks() / 1000.0;
    if (now - lastProbe > 2.0) { lastProbe = now; Probe(); }
    SyncDeviceHold();

    // F11 or Alt+Enter: the client in or out of full screen
    if (ImGui::IsKeyPressed(ImGuiKey_F11, false) ||
        (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && ImGui::GetIO().KeyAlt))
        SetFullscreen(!fullscreen);

    // LB / RB change page (not while mapping)
    if (captureOut < 0 && !ImGui::GetIO().WantTextInput) {
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadL1, false)) page = Page((page + PAGE_COUNT - 1) % PAGE_COUNT);
        if (ImGui::IsKeyPressed(ImGuiKey_GamepadR1, false)) page = Page((page + 1) % PAGE_COUNT);
    }

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
    ImGui::PushStyleColor(ImGuiCol_WindowBg, V(col::bg));
    ImGui::Begin("root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    const float sideW = 250;
    ImGui::BeginChild("side", { sideW, vp->Size.y }, 0, ImGuiWindowFlags_NoScrollbar);
    Sidebar(sideW, vp->Size.y);
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 44, 34 });
    ImGui::BeginChild("main", { 0, vp->Size.y }, ImGuiChildFlags_AlwaysUseWindowPadding);
    switch (page) {
    case PLAY: Header("PLAY", "Your game, your controller, ready to fight."); PagePlay(); break;
    case CONTROLS: PageControls(); break;
    case SETTINGS: PageSettings(); break;
    case INSTALL: PageInstall(); break;
    default: break;
    }
    ImGui::Dummy({ 0, 20 });
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // a short note at the bottom (saved, errors)
    if (!settingsNote.empty() && now - settingsNoteAt < 3.0) {
        ImDrawList* d = ImGui::GetForegroundDrawList();
        ImGui::PushFont(F.bold, 14.f);
        const ImVec2 ts = ImGui::CalcTextSize(settingsNote.c_str());
        const ImVec2 p = { vp->Pos.x + sideW + (vp->Size.x - sideW - ts.x) * 0.5f - 18, vp->Pos.y + vp->Size.y - 64 };
        d->AddRectFilled(p, { p.x + ts.x + 36, p.y + 38 }, col::cardHi, 19.f);
        d->AddRect(p, { p.x + ts.x + 36, p.y + 38 }, Alpha(col::gold, 0.5f), 19.f);
        d->AddText({ p.x + 18, p.y + 10 }, col::text, settingsNote.c_str());
        ImGui::PopFont();
    }
    ImGui::End();
}

// ---------------------------------------------------------------- App
#include "res/fonts.h"
#include "res/mark.h"

App::App() : p_(new Impl) {}
App::~App() { delete p_; }

void App::Init(SDL_Window* window, float scale)
{
    p_->window = window;
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad;
    ImFontConfig cfg;
    cfg.FontDataOwnedByAtlas = false;
    F.body = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontBody), int(kFontBodySize), 15.f, &cfg);
    F.bold = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontBodyBold), int(kFontBodyBoldSize), 15.f, &cfg);
    F.display = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontDisplay), int(kFontDisplaySize), 22.f, &cfg);
    F.heavy = io.Fonts->AddFontFromMemoryTTF(const_cast<unsigned char*>(kFontDisplayHeavy), int(kFontDisplayHeavySize), 30.f, &cfg);
    io.FontDefault = F.body;

    ImGuiStyle& s = ImGui::GetStyle();
    s.FrameRounding = 6;
    s.GrabRounding = 6;
    s.PopupRounding = 8;
    s.ScrollbarRounding = 6;
    s.ScrollbarSize = 10;
    s.FramePadding = { 12, 8 };
    s.ItemSpacing = { 10, 8 };
    s.WindowBorderSize = 0;
    s.ChildBorderSize = 1;
    s.CellPadding = { 8, 4 };
    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = V(col::text);
    c[ImGuiCol_TextDisabled] = V(col::faint);
    c[ImGuiCol_WindowBg] = V(col::bg);
    c[ImGuiCol_ChildBg] = V(0);
    c[ImGuiCol_PopupBg] = V(col::cardHi);
    c[ImGuiCol_Border] = V(col::line);
    c[ImGuiCol_FrameBg] = V(IM_COL32(12, 12, 16, 255));
    c[ImGuiCol_FrameBgHovered] = V(IM_COL32(30, 30, 38, 255));
    c[ImGuiCol_FrameBgActive] = V(IM_COL32(36, 36, 46, 255));
    c[ImGuiCol_SliderGrab] = V(col::gold);
    c[ImGuiCol_SliderGrabActive] = V(col::gold);
    c[ImGuiCol_Header] = V(Alpha(col::gold, 0.2f));
    c[ImGuiCol_HeaderHovered] = V(Alpha(col::gold, 0.3f));
    c[ImGuiCol_HeaderActive] = V(Alpha(col::gold, 0.4f));
    c[ImGuiCol_CheckMark] = V(col::gold);
    c[ImGuiCol_ScrollbarBg] = V(0);
    c[ImGuiCol_ScrollbarGrab] = V(col::line);
    c[ImGuiCol_NavCursor] = V(col::gold);
    c[ImGuiCol_TextSelectedBg] = V(Alpha(col::gold, 0.35f));
    s.ScaleAllSizes(scale);
    s.FontScaleDpi = scale;

    if (SDL_Renderer* r = SDL_GetRenderer(window)) {
        if (SDL_Surface* bmp = SDL_LoadBMP_IO(SDL_IOFromConstMem(kMarkBmp, kMarkBmpSize), true)) {
            g_mark = SDL_CreateTextureFromSurface(r, bmp);
            if (g_mark) SDL_SetTextureBlendMode(g_mark, SDL_BLENDMODE_BLEND);
            SDL_DestroySurface(bmp);
        }
    }
    p_->Load();
    int n = 0;
    if (SDL_JoystickID* ids = SDL_GetJoysticks(&n)) {
        for (int i = 0; i < n; ++i) p_->AddDevice(ids[i]);
        SDL_free(ids);
    }
}

void App::Event(const SDL_Event& e)
{
    if (e.type == SDL_EVENT_JOYSTICK_ADDED) p_->AddDevice(e.jdevice.which);
    else if (e.type == SDL_EVENT_JOYSTICK_REMOVED) p_->RemoveDevice(e.jdevice.which);
}

void App::Frame() { p_->Frame(); }
void App::SetPage(int page) { p_->page = Impl::Page(std::clamp(page, 0, int(Impl::PAGE_COUNT) - 1)); }
