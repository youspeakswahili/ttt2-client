// TTT2 Client - the UI. One window, a sidebar and four pages (Play, Controls, Settings, Install), all
// usable with a controller (D-pad / stick to move, A to choose).
#include "app.h"
#include "builder.h"
#include "updater.h"
#include "online.h"
#include "version.h"
#include "ini.h"
#include "platform.h"
#include "../common/input_config.h"
#include "net.h"
#include "roster.h"

#include <SDL3/SDL.h>
#include "imgui.h"
#include "imgui_internal.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <map>
#include <mutex>
#include <cstring>
#include <cstdlib>
#include <ctime>

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
// A Windows virtual-key code as a player knows it: "Caps Lock", "Mouse 4 (back)", "Right Ctrl", "F12", "Num 5"...
std::string VkName(int vk)
{
    if (vk >= 'A' && vk <= 'Z') return std::string(1, char(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, char(vk));
    if (vk >= 0x60 && vk <= 0x69) return "Num " + std::to_string(vk - 0x60);
    if (vk >= 0x70 && vk <= 0x87) return "F" + std::to_string(vk - 0x70 + 1);
    switch (vk) {
    case 0x04: return "Middle mouse button";
    case 0x05: return "Mouse 4 (back)";
    case 0x06: return "Mouse 5 (forward)";
    case 0x08: return "Backspace";
    case 0x09: return "Tab";
    case 0x0C: return "Clear";
    case 0x0D: return "Enter";
    case 0x13: return "Pause";
    case 0x14: return "Caps Lock";
    case 0x20: return "Space";
    case 0x21: return "Page Up";
    case 0x22: return "Page Down";
    case 0x23: return "End";
    case 0x24: return "Home";
    case 0x25: return "Left arrow";
    case 0x26: return "Up arrow";
    case 0x27: return "Right arrow";
    case 0x28: return "Down arrow";
    case 0x2D: return "Insert";
    case 0x2E: return "Delete";
    case 0x6A: return "Num *";
    case 0x6B: return "Num +";
    case 0x6D: return "Num -";
    case 0x6E: return "Num .";
    case 0x6F: return "Num /";
    case 0x90: return "Num Lock";
    case 0x91: return "Scroll Lock";
    case 0xA0: return "Left Shift";
    case 0xA1: return "Right Shift";
    case 0xA2: return "Left Ctrl";
    case 0xA3: return "Right Ctrl";
    case 0xA4: return "Left Alt";
    case 0xA5: return "Right Alt";
    default: break;
    }
    const std::string n = plat::KeyName(vk);   // the layout's own name: ; ' [ ] ö ä ...
    if (!n.empty()) return n;
    char t[16];
    std::snprintf(t, sizeof t, "Key %02X", vk);
    return t;
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
// The opponent's connection as fighting games show it: signal bars by the measured ping (grey until measured) and an icon,
// wired (a network plug) or Wi-Fi (arcs); a question mark when their client did not say.
void ConnectionBadge(int conn, int rtt)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    const int filled = rtt <= 0 ? 0 : rtt <= 50 ? 4 : rtt <= 90 ? 3 : rtt <= 140 ? 2 : 1;
    const ImU32 on = filled >= 3 ? col::green : filled == 2 ? col::amber : col::red, off = col::line;
    for (int i = 0; i < 4; ++i) {
        const float h = 6.f + 4.f * float(i), x = p.x + float(i) * 6.f;
        d->AddRectFilled({ x, p.y + 20.f - h }, { x + 4.f, p.y + 20.f }, i < filled ? on : off, 1.f);
    }
    const float ix = p.x + 34.f, cy = p.y + 10.f;
    const ImU32 ic = col::text;
    if (conn == 1) {
        // a plug: body, three pins, the cable
        d->AddRectFilled({ ix, cy - 4.f }, { ix + 16.f, cy + 5.f }, ic, 2.f);
        for (int k = 0; k < 3; ++k) d->AddRectFilled({ ix + 3.f + 4.f * float(k), cy - 8.f }, { ix + 5.f + 4.f * float(k), cy - 4.f }, ic);
        d->AddRectFilled({ ix + 6.f, cy + 5.f }, { ix + 10.f, cy + 10.f }, ic);
    } else if (conn == 2) {
        const ImVec2 c{ ix + 8.f, cy + 8.f };
        for (int k = 1; k <= 3; ++k) d->PathArcTo(c, 4.f * float(k), -2.36f, -0.79f, 12), d->PathStroke(ic, 0, 2.f);
        d->AddCircleFilled(c, 1.8f, ic);
    } else {
        d->AddText(F.bold, 16.f, { ix + 3.f, cy - 9.f }, col::faint, "?");
    }
    ImGui::Dummy({ 58.f, 22.f });
}

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
const char kTestLogsNote[] =
    "All the matches' logs are in one file here: test log.txt (the newest match at the end).\r\n"
    "Each match has its own part, headed MATCH <time>, with the games it finished, the game's step-by-step trace\r\n"
    "and its full log. When something went wrong, send test log.txt. The last 5 matches are kept, 4 MB at most.\r\n";

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
    { O_M23, "2+3", "Macro", false }, { O_M14, "1+4", "Macro", false },
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

// What the player's own controller calls each button (SDL's gamepad type)
enum PadFamily { FAM_XBOX, FAM_PLAYSTATION, FAM_NINTENDO, FAM_OTHER };
PadFamily FamilyOf(SDL_Gamepad* pad)
{
    if (!pad) return FAM_OTHER;
    switch (SDL_GetGamepadType(pad)) {
    case SDL_GAMEPAD_TYPE_PS3: case SDL_GAMEPAD_TYPE_PS4: case SDL_GAMEPAD_TYPE_PS5: return FAM_PLAYSTATION;
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_PRO: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_LEFT:
    case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_RIGHT: case SDL_GAMEPAD_TYPE_NINTENDO_SWITCH_JOYCON_PAIR: return FAM_NINTENDO;
    case SDL_GAMEPAD_TYPE_XBOX360: case SDL_GAMEPAD_TYPE_XBOXONE: return FAM_XBOX;
    default: return FAM_XBOX;   // SDL's standard layout is Xbox-shaped
    }
}
// A controller SDL does not know as a gamepad, by its maker: most fight sticks and licensed pads (Victrix, Hori, Razer,
// Qanba, Mad Catz, Nacon, Brook) are PlayStation ones and use the PlayStation layout GenericMap assumes.
PadFamily FamilyOfVendor(uint16_t vendor)
{
    switch (vendor) {
    case 0x054C: case 0x0E6F: case 0x0F0D: case 0x1532: case 0x2C22: case 0x0738: case 0x146B: case 0x0C12: return FAM_PLAYSTATION;
    case 0x045E: return FAM_XBOX;
    case 0x057E: return FAM_NINTENDO;
    default: return FAM_OTHER;
    }
}

PadFamily DeviceFamily(const Device& d)
{
    return d.pad ? FamilyOf(d.pad) : FamilyOfVendor(d.joy ? SDL_GetJoystickVendor(d.joy) : 0);
}

// The output's physical button on that controller family (by position: west, north, south, east)
const char* PadButtonName(PadFamily f, int out)
{
    static const char* xbox[O_COUNT] = { "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Start", "Back", "LS", "RS", "LB", "RB", "A", "B", "X", "Y", "LT", "RT" };
    static const char* ps[O_COUNT] = { "D-pad up", "D-pad down", "D-pad left", "D-pad right", "Options", "Share", "L3", "R3", "L1", "R1", "Cross", "Circle", "Square", "Triangle", "L2", "R2" };
    static const char* nin[O_COUNT] = { "D-pad up", "D-pad down", "D-pad left", "D-pad right", "+", "-", "LS", "RS", "L", "R", "B", "A", "Y", "X", "ZL", "ZR" };
    if (out < 0 || out >= O_M12) return "";
    return f == FAM_PLAYSTATION ? ps[out] : f == FAM_NINTENDO ? nin[out] : xbox[out];
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
    enum Page { PLAY, ONLINE, CONTROLS, SETTINGS, INSTALL, PAGE_COUNT } page = PLAY;

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

    // online play (ttt2d)
    std::unique_ptr<Online> online;
    // A match handed to the game (TTT2 Online.exe): its process, which match, and its result.
    // The game writes each finished game into TTT2_RESULT as it decides it (one line: result=won|lost|draw). When the
    // game closes, the client sends the set's winner (more games won) to the server by itself - nobody is asked. A set
    // that ended level, or a game that closed without a finished match, sends nothing.
    uintptr_t matchProc = 0;
    std::string matchStarted, matchNote;
    struct SetScore { int won = 0, lost = 0, draws = 0; int Games() const { return won + lost + draws; } };
    SetScore setScore;              // read while the game runs and once more when it has closed
    std::string resultsFor;         // the match the results file belongs to (kept when the game is started again for it)
    bool resultSent = false;        // this match's result went to the server
    bool noResult = false;          // the game closed with nothing to send
    std::string lastSet, lastSetFor;   // the last reported match in words, for the lobby, and which match it was
    double scoreReadAt = 0;
    MatchInfo resultFor;
    // How the game gets into a match: the way 0.3 did it (the game opens on its menu, 1P creates the session, 2P is taken
    // into it) - the one that connects. The "straight in" start (boots to character select) does not work yet (Test 32,
    // 2026-11-24: the online sign-in it starts is the ONLINE MODE entry itself and took the game there mid-select), so no
    // setting offers it; [dev] straight=1 in client.ini turns it on for work on it. A choice saved by Test 32 is ignored.
    bool StraightIn() { return client.Get("dev", "straight", "0") == "1"; }
    // How a match starts (2026-11-24, the user: "do it his way except for pair play"):
    //   direct  the game boots straight into the fight as an offline versus match and the two PCs sync their inputs
    //           themselves (the game's direct_versus.cpp) - no online menus. TAG and SOLO teams.
    //   room    the game's own online room, the way 0.3 did it: only PAIR PLAY (two players per PC) uses it now - the
    //           1v1 fallback to it was taken out (2026-11-26, the user: "We don't need the old room mode").
    bool PairPlay() { return client.Get("picks", "team", "duo") == "pair"; }
    bool RoomStart() { return PairPlay(); }
    bool matchDirect = false;       // the running / last game was started the direct way
    std::string matchTeam;          // its team in words, for the match card
    static std::string ResultsFile() { return plat::JoinPath(plat::ClientDataDir(), "match_results.txt"); }
    // The game's own ranks (2026-11-24, the user: "the games actual rank system"): each character's online rank as the
    // player's save has it. The game writes them into TTT2_RANKS whenever they change (every start from the client);
    // a ranked match's result file also says what the match did to the two characters played ("rank c=.. before=..
    // after=.." lines), which the lobby shows under the last match.
    static std::string RanksFile() { return plat::JoinPath(plat::ClientDataDir(), "ranks.txt"); }
    int gameRank[64];               // by the game's character number; -1 not known
    bool ranksKnown = false;
    double ranksReadAt = -10;
    void ReadRanks();
    struct RankChange { int character = -1, before = 0, after = 0, pointsBefore = 0, pointsAfter = 0; };
    std::vector<RankChange> lastRanks;      // the last reported match's (lastSetFor)
    // the server's ranks (Online::View::chars): written for the game (TTT2_SERVER_RANKS) and shown instead of the save's
    static std::string ServerRanksFile() { return plat::JoinPath(plat::ClientDataDir(), "server_ranks.txt"); }
    int serverCharsVersion = -1;
    bool serverRanks = false;
    std::string serverRanksFor;             // the rated match the server's rank changes were last taken from
    void TakeServerRanks();
    std::string FightsJson();
    static std::vector<RankChange> ReadRankChanges();
    static SetScore ReadScore();
    bool StartMatch(const MatchInfo& m);
    void MatchGameClosed();
    void TickMatch();
    // Practice in the game (TTT2_BOOT=practice), also while searching: closed for a match when one is found.
    uintptr_t practiceProc = 0;
    std::string practiceNote;
    bool EnsureOnlineExe(std::string& note);
    void StartPractice();
    void TeamCard();
    void PracticeCard();
    void ReplaysCard();
    void StartReplay(const std::string& file);
    static std::string ReplaysDir() { return plat::JoinPath(plat::ClientDataDir(), "replays"); }
    std::string replayNote;
    bool CharacterCombo(const char* id, const char* key, bool allowRandom);
    void PracticeQuickCard();
    // test builds: the game's trace and log of the last match, copied next to the client ("test logs")
    // Test builds: every match's trace, game log and results, kept under the time the game started (the last 10 matches).
    // In the game's own data folder (%LOCALAPPDATA%\TTT2Recomp\test logs; the solo test's player 2 has its own).
    static std::string TestLogsDir() { return plat::JoinPath(plat::GameDataDir(), "test logs"); }
    std::string logStamp;           // the running match's file name prefix
    void PruneTestLogs();
    // accepting a found match: the question (also shown in the game while practising), its answer from there
    std::string promptFor;          // the found match last announced (sound, taskbar flash, the game's question)
    bool promptInGame = false;      // the game is showing the question
    bool acceptNavOff = false;      // the pad / keyboard answer the question instead of moving around the client
    void TickAccept();
    void WritePrompt(const Online::View* v);
    // voice chat: the push-to-talk key being chosen, the sound devices, the live settings file the game re-reads
    bool pttListen = false;
    std::string pttTarget = "key";   // which key is being chosen: "key" (push to talk) or "mute_key" (open mic)
    bool pttHeld[256] = {};
    int pttCooldown = 0;
    void StartPttListen(const char* target);
    void KeyBindRow(const char* target, const char* title, const char* hint, int def);
    void StopPttListen();
    void PollPttListen();
    std::vector<plat::AudioDevice> mics, outs;
    std::string defMic, defOut;
    double audioListAt = -100;
    void RefreshAudio();
    bool AudioCombo(const char* id, const char* key, bool microphone, float width);
    bool liveDirty = false;
    double liveWrittenAt = 0;
    void WriteVoiceLive();
    const char* GameKeyClash(int vk);
    std::vector<std::pair<std::string, std::string>> CommonEnv();
    std::vector<std::pair<std::string, std::string>> GraphicsEnv();
    int qMode = 0, qSide = 0, qPing = 2;   // ranked/casual, any/1P/2P, max ping choice
    char nameBuf[32] = {};
    bool renamingOpen = false;
    char renameBuf[32] = {};
    double lastBoard = -100;
    static constexpr int kPings[5] = { 60, 90, 120, 150, 200 };
    void StartOnline()
    {
        if (online) return;
        ServerConfig cfg;
        cfg.host = client.Get("server", "host", cfg.host);
        cfg.port = client.GetInt("server", "port", cfg.port);
        cfg.fingerprint = client.Get("server", "fingerprint", cfg.fingerprint);
        online = std::make_unique<Online>(cfg, plat::ClientDataDir());
        online->clientVersion = TTT2_CLIENT_VERSION;
        online->Start();
        qMode = std::clamp(client.GetInt("online", "mode", 0), 0, 1);
        qSide = std::clamp(client.GetInt("online", "side", 0), 0, 2);
        qPing = std::clamp(client.GetInt("online", "ping", 2), 0, 4);
    }
    void SaveQueuePrefs()
    {
        client.SetInt("online", "mode", qMode);
        client.SetInt("online", "side", qSide);
        client.SetInt("online", "ping", qPing);
        SaveClient();
    }

    // the client's own updates
    Updater updater;
    bool wantsQuit = false, justUpdated = false, layerOutdated = false, autoInstall = false;
    std::string updateNote;
    void CheckLayer()
    {
        // the game's copy of the controller layer against the one that came with this client
        std::string mine, theirs;
        // the installed game build against the one that came with this client
        const std::string engine = plat::JoinPath(Builder::EngineDir(), plat::kGameExe);
        layerOutdated = installed && plat::ReadBinary(engine, mine) &&
                        plat::ReadBinary(plat::JoinPath(installDir, plat::kGameExe), theirs) && mine != theirs;
    }
    void UpdateLayer()
    {
        BuildRequest r;
        r.dest = installDir;
        r.onlyLayer = true;
        builder.Start(r);
    }
    void InstallUpdate()
    {
        std::string err;
        if (!updater.Apply(err)) { updateNote = err; return; }
        if (plat::Relaunch("--updated")) wantsQuit = true;
        else updateNote = "Updated. Close and reopen the client to finish.";
    }

    // install page
    Builder builder;
    std::string iso, tu, dest;
    std::mutex dialogLock;
    std::string* dialogTarget = nullptr;
    std::string dialogResult;
    bool dialogDone = false;

    void Load()
    {
        std::string t;
        std::fill(std::begin(gameRank), std::end(gameRank), -1);
        ReadRanks();
        if (plat::ReadText(plat::JoinPath(plat::ClientDataDir(), "client.ini"), t)) client.Parse(t);
        // Picks saved by clients before 0.4.0's renumbering (the roster had most characters from Mokujin on one off -
        // Forest Law came out as Slim Bob): the same characters by the game's own numbers.
        if (client.GetInt("picks", "numbers", 1) < 2) {
            static constexpr int kOldToNew[64] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20, 21, 22, 23, 24, 25, 26,
                                                   27, 28, 29, 30, 31, 32, 43, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 44, 45, 46, 47, 48, 50, 51,
                                                   52, 53, 54, 55, 56, 57, 58, 49, 18, -1, -1, -1, -1, 63 };
            for (const auto& [section, key] : { std::pair{ "picks", "c1" }, { "picks", "c2" }, { "practice", "o1" }, { "practice", "o2" } }) {
                const int was = client.GetInt(section, key, -1);
                if (was >= 0 && was < 64 && kOldToNew[was] >= 0) client.Set(section, key, std::to_string(kOldToNew[was]));
            }
            client.Set("picks", "numbers", "2");
            SaveClient();
        }
        installDir = client.Get("client", "install_dir");
        iso = client.Get("client", "iso");
        tu = client.Get("client", "tu");
        fullscreen = client.GetInt("client", "fullscreen", 0) != 0;
        if (fullscreen) SDL_SetWindowFullscreen(window, true);
        if (installDir.empty() || !plat::LooksLikeInstall(installDir)) {
            const std::string reg = plat::RegisteredInstall();
            if (plat::LooksLikeInstall(reg)) installDir = reg;
        }
        dest = plat::LooksLikeInstall(installDir) ? installDir : plat::DefaultInstallDir();
        if (plat::ReadText(plat::JoinPath(plat::ClientDataDir(), "input.ini"), t)) ParseConfig(t, input);
        LoadGameSettings();
        Probe();
        CheckLayer();
        Updater::CleanUpOldFiles();
        updater.Check();
        if (!std::getenv("TTT2_NO_ONLINE")) StartOnline();
    }
    void SaveClient()
    {
        client.Set("client", "install_dir", installDir);
        client.Set("client", "iso", iso);
        client.Set("client", "tu", tu);
        plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "client.ini"), client.Text());
    }
    void SaveInput() { plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "input.ini"), WriteConfig(input)); }
    void LoadGameSettings()
    {
        std::string t;
        game = {};
        if (plat::ReadText(plat::JoinPath(plat::ClientDataDir(), "game.ini"), t)) game.Parse(t);
        gameLoaded = true;
        // 5K (4x) is gone: it needs about twice 4K's video memory
        if (game.GetInt("Graphics", "Scale", 2) > 3) {
            game.SetInt("Graphics", "Scale", 3);
            plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "game.ini"), game.Text());
        }
    }
    void SaveGameSettings()
    {
        const bool ok = plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "game.ini"), game.Text());
        settingsNote = !ok ? "Could not save the game's settings." : gameRunning ? "Saved. Restart the game to use them." : "Saved.";
        settingsNoteAt = SDL_GetTicks() / 1000.0;
    }
    void Probe()
    {
        installed = plat::LooksLikeInstall(installDir);
        layerInstalled = installed;   // controllers are part of the game build now
        installedVersion = installed ? plat::FileVersion(plat::JoinPath(installDir, plat::kGameExe)) : "";
        gameRunning = plat::IsGameRunning() || matchProc != 0;
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
    void UpdateBanner();
    void Sidebar(float w, float h);
    void Header(const char* title, const char* subtitle);
    void PagePlay();
    void PageOnline();
    void PageControls();
    void PageSettings();
    void PageInstall();
    void Visualizer(Device* d, float height);
    bool ShowAsStick(const Device& d);
    bool PictureHeld(const Device& d, int out);
    void DrawPadPicture(Device* dv, ImVec2 p, float w, float height);
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
    static const char* names[] = { "PLAY", "ONLINE", "CONTROLS", "SETTINGS", "INSTALL" };
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
        ImGui::SetCursorPos({ 20, h - 260 });
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
    ImGui::SetCursorPos({ 24, h - 196 });
    ImGui::BeginGroup();
    Text(F.bold, 11.5f, col::faint, "GAME");
    Text(F.bold, 14.f, installed ? col::text : col::amber, installed ? (gameRunning ? "Running" : "Installed") : "Not installed");
    ImGui::Dummy({ 0, 6 });
    Text(F.bold, 11.5f, col::faint, "CONTROLLERS");
    char t[64];
    std::snprintf(t, sizeof t, devicesSuspended ? "In use by the game" : devices.empty() ? "None connected" : "%zu connected", devices.size());
    Text(F.bold, 14.f, devices.empty() && !devicesSuspended ? col::dim : col::text, t);
    ImGui::Dummy({ 0, 6 });
    Text(F.body, 11.5f, col::faint, "D-pad  move   ·   A  choose");
    ImGui::EndGroup();

    // the client's version and its update check, always in view
    ImGui::SetCursorPos({ 24, h - 52 });
    ImGui::BeginGroup();
    {
        const Updater::State st = updater.GetState();
        std::string v = std::string("Client ") + TTT2_CLIENT_VERSION;
        ImU32 c = col::faint;
        if (st == Updater::UP_TO_DATE) v += "  ·  up to date";
        else if (st == Updater::CHECKING) v += "  ·  checking...";
        else if (st == Updater::AVAILABLE || st == Updater::DOWNLOADING || st == Updater::READY) { v += "  ·  " + updater.Latest() + " ready"; c = col::gold; }
        else if (st == Updater::FAILED) { v += "  ·  check failed"; c = col::amber; }
        Text(F.bold, 11.5f, c, v.c_str());
        ImGui::PushStyleColor(ImGuiCol_Text, V(col::gold));
        ImGui::PushFont(F.bold, 12.f);
        if (st != Updater::CHECKING && st != Updater::DOWNLOADING && ImGui::SmallButton("Check for updates")) updater.Check();
        ImGui::PopFont();
        ImGui::PopStyleColor();
    }
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
    std::string sub = installed ? "Game build " + client.Get("client", "installed_tag", Builder::kBuildTag) + "  ·  " + installDir
                                : "Build the game from your own disc image to start.";
    Text(F.body, 14.f, col::dim, sub.c_str());
    ImGui::Dummy({ 0, 18 });
    if (installed) {
        if (BigButton(gameRunning ? "RUNNING" : "LAUNCH GAME", { 260, 56 }, !gameRunning)) {
            // the game as it is (menus, Customize...), with the client's settings - unlocks included - when the online
            // build is there; otherwise the port's own exe
            std::string err, note;
            if (!EnsureOnlineExe(note)) { settingsNote = note; settingsNoteAt = SDL_GetTicks() / 1000.0; }
            else {
                practiceProc = plat::LaunchWithEnv(installDir, plat::kGameExe, CommonEnv(), err);
                if (practiceProc) gameRunning = true;
                else { settingsNote = "Could not start the game: " + err; settingsNoteAt = SDL_GetTicks() / 1000.0; }
            }
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
    if (layerInstalled && layerOutdated) {
        Check(false, "Game build out of date", "This client has a newer one for the game's folder.");
        ImGui::SameLine();
        if (Button(builder.Running() ? "Updating..." : "Update it", { 110, 28 }, !builder.Running() && !gameRunning)) UpdateLayer();
    } else {
        Check(layerInstalled, "Every controller supported", layerInstalled ? "Pads, sticks and keyboard work in the game build." : "Set up the game first.");
    }
    const bool delayOff = game.GetInt("Game", "InputDelay", 0) == 0;
    Check(delayOff, "Offline input delay removed", delayOff ? "The original game's 2 extra frames are gone." : "Turned back on in Settings.");
    {
        static const char* q[] = { "", "Original 720p", "Upscaled 1440p", "Upscaled 4K" };
        const std::string g = std::string(q[std::clamp(game.GetInt("Graphics", "Scale", 2), 1, 3)]) + ", 60 fps locked. Change it in Settings → Graphics.";
        Check(true, "Native PC graphics", g.c_str());
    }
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
    {
        const Online::View ov = online ? online->Snapshot() : Online::View{};
        ImGui::PushFont(F.body, 14.f);
        if (ov.state == Online::LOBBY || ov.state == Online::QUEUED || ov.state == Online::CHECKING || ov.state == Online::READY) {
            char t[160];
            std::snprintf(t, sizeof t, "Signed in as %s  ·  rating %d  ·  %d online now", ov.name.c_str(), ov.rating, ov.online);
            Wrapped(col::text, t);
        } else if (ov.state == Online::NEED_NAME) {
            Wrapped(col::dim, "Pick your fighter name on the ONLINE page to play ranked and casual matches.");
        } else {
            Wrapped(col::dim, ov.error.empty() ? "Connecting to the server..." : ov.error.c_str());
        }
        ImGui::PopFont();
        ImGui::Dummy({ 0, 8 });
        if (Button("Go online", { 140, 32 })) page = ONLINE;
    }
    EndCard();
    ImGui::EndChild();
    ImGui::Dummy({ 0, 12 });
    PracticeCard();
    ImGui::Dummy({ 0, 12 });
    ReplaysCard();
}

void App::Impl::PageOnline()
{
    Header("ONLINE", "Ranked and casual matches against players on the TTT2 server, on the side you choose.");
    if (!online) StartOnline();
    const Online::View v = online->Snapshot();
    const double now = SDL_GetTicks() / 1000.0;

    if (!v.notice.empty()) {
        ImGui::PushFont(F.bold, 14.f);
        Wrapped(col::amber, v.notice.c_str());
        ImGui::PopFont();
        ImGui::Dummy({ 0, 6 });
    }

    if (v.state == Online::NEED_NAME) {
        BeginCard("name", 0, "Pick your fighter name");
        ImGui::PushFont(F.body, 14.f);
        Wrapped(col::dim, "This is your name on the server and the leaderboard: 3 to 16 letters, digits, _ or -. "
                          "Your account is saved on this PC; no email or password needed.");
        ImGui::PopFont();
        if (!v.error.empty()) { ImGui::Dummy({ 0, 4 }); Text(F.bold, 14.f, col::red, v.error.c_str()); }
        ImGui::Dummy({ 0, 8 });
        ImGui::SetNextItemWidth(320);
        ImGui::PushFont(F.bold, 16.f);
        const bool enter = ImGui::InputText("##name", nameBuf, sizeof nameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopFont();
        ImGui::SameLine(0, 12);
        const bool okLen = std::strlen(nameBuf) >= 3;
        if ((BigButton("CREATE", { 160, 40 }, okLen) || (enter && okLen))) online->Register(nameBuf);
        EndCard();
        return;
    }
    if (v.state == Online::CONNECTING || v.state == Online::OFFLINE || v.state == Online::FAILED) {
        BeginCard("conn", 0, v.state == Online::FAILED ? "Can't reach the server" : "Connecting");
        ImGui::PushFont(F.body, 14.f);
        Wrapped(v.state == Online::FAILED ? col::red : col::dim, v.state == Online::FAILED ? (v.error + " Trying again shortly.").c_str() : "Connecting to the TTT2 server...");
        ImGui::PopFont();
        EndCard();
        return;
    }

    if (now - lastBoard > 60 && v.state == Online::LOBBY) { online->RequestBoard(); lastBoard = now; }

    // you + the server
    const float half = (ImGui::GetContentRegionAvail().x - 18) * 0.5f;
    ImGui::BeginChild("me", { half, 0 }, ImGuiChildFlags_AutoResizeY);
    BeginCard("mecard");
    if (!renamingOpen) {
        Text(F.heavy, 34.f, col::text, v.name.c_str());
        ImGui::SameLine(0, 12);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 8);
        if (Button("Change name", { 120, 28 }, v.state == Online::LOBBY)) {
            renamingOpen = true;
            std::snprintf(renameBuf, sizeof renameBuf, "%s", v.name.c_str());
        }
    } else {
        ImGui::SetNextItemWidth(220);
        const bool enter = ImGui::InputText("##rename", renameBuf, sizeof renameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine(0, 8);
        if (Button("Save", { 80, 30 }, std::strlen(renameBuf) >= 3) || (enter && std::strlen(renameBuf) >= 3)) {
            online->Rename(renameBuf);
            renamingOpen = false;
        }
        ImGui::SameLine(0, 8);
        if (Button("Cancel", { 80, 30 })) renamingOpen = false;
        Text(F.body, 12.5f, col::faint, "3-16 letters, digits, - or _. Your rating and record stay.");
    }
    if (!v.error.empty() && v.state == Online::LOBBY) Text(F.body, 13.f, col::red, v.error.c_str());
    char t[160];
    std::snprintf(t, sizeof t, "Rating %d   ·   %d wins, %d losses", v.rating, v.wins, v.games - v.wins);
    Text(F.bold, 15.f, col::gold, t);
    if (!v.lastOutcome.empty()) {
        // what the server made of the last match: the set as the game reported it, and the rating change if it was ranked
        std::string line = !lastSet.empty() && lastSetFor == v.lastMatch ? lastSet : "Last match";
        if (v.lastOutcome == "disputed") line += ": the two games reported different winners, so it was not counted.";
        else if (v.lastOutcome == "abandoned") line += ": not counted.";
        else {
            if (v.lastOutcome == "forfeit") line += " (your opponent left)";
            if (v.lastChange != 0) { std::snprintf(t, sizeof t, "   ·   rating %+d", v.lastChange); line += t; }
        }
        ImGui::PushFont(F.body, 13.f);
        Wrapped(v.lastOutcome == "disputed" ? col::amber : v.lastChange > 0 ? col::green : v.lastChange < 0 ? col::red : col::dim, line.c_str());
        // ... and what it did to the game's own ranks of the characters played (a ranked match)
        if (lastSetFor == v.lastMatch)
            for (const RankChange& r : lastRanks) {
                std::string rl = roster::CharacterName(r.character);
                if (r.after > r.before) rl += std::string(": promoted to ") + roster::RankName(r.after) + " (from " + roster::RankName(r.before) + ")";
                else if (r.after < r.before) rl += std::string(": demoted to ") + roster::RankName(r.after) + " (from " + roster::RankName(r.before) + ")";
                else {
                    // no new rank: the game's own points toward the next one (it never shows them itself)
                    char pts[48];
                    std::snprintf(pts, sizeof pts, " (%+d points)", r.pointsAfter - r.pointsBefore);
                    rl += std::string(": stays ") + roster::RankName(r.after) + pts;
                }
                Wrapped(r.after > r.before ? col::gold : r.after < r.before ? col::red : col::dim, rl.c_str());
            }
        ImGui::PopFont();
    }
    EndCard();
    ImGui::EndChild();
    ImGui::SameLine(0, 18);
    ImGui::BeginChild("srv", { half, 0 }, ImGuiChildFlags_AutoResizeY);
    BeginCard("srvcard");
    Text(F.display, 20.f, col::text, "Server");
    std::snprintf(t, sizeof t, "%d online   ·   %d searching   ·   %d in matches", v.online, v.queued, v.playing);
    Text(F.body, 14.f, col::dim, t);
    ImGui::SameLine(ImGui::GetContentRegionAvail().x - 110 + ImGui::GetStyle().WindowPadding.x);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 6);
    if (Button("Test logs", { 110, 28 })) { plat::WriteText(plat::JoinPath(TestLogsDir(), "what to send.txt"), kTestLogsNote); plat::OpenFolder(TestLogsDir()); }
    // test builds: a second player on this PC, to test a match without anyone else
    if (plat::SecondPlayer()) {
        Text(F.body, 13.f, col::dim, "This window is PLAYER 2 of the solo test: its own account, settings and saves.");
    } else {
        if (Button("Solo test: open player 2", { 230, 28 })) plat::Relaunch("--player2");
        Text(F.body, 13.f, col::dim, "A second client on this PC, with its own account. Find a match in both.");
    }
    EndCard();
    ImGui::EndChild();
    ImGui::Dummy({ 0, 12 });

    if (v.state == Online::LOBBY) {
        // the team first: matches go straight into the fight, so the characters are chosen here
        TeamCard();
        ImGui::Dummy({ 0, 12 });
        BeginCard("queue", 0, "Find a match");
        static const char* modes[] = { "Ranked", "Casual" };
        static const char* sides[] = { "Either side", "1P (left)", "2P (right)" };
        static const char* pings[] = { "60 ms", "90 ms", "120 ms", "150 ms", "200 ms" };
        const float ctl = 420;
        RowLabel("Mode", "Ranked changes your rating. Casual doesn't.", ctl);
        if (Segmented("mode", &qMode, modes, 2, ctl)) SaveQueuePrefs();
        Divider();
        RowLabel("Side", "A locked side is guaranteed: you are never matched with someone who locked the same side.", ctl);
        if (Segmented("side", &qSide, sides, 3, ctl)) SaveQueuePrefs();
        Divider();
        RowLabel("Max ping", "Only opponents your PC can reach within this. Lower = better connections, longer waits.", ctl);
        if (Segmented("ping", &qPing, pings, 5, ctl)) SaveQueuePrefs();
        ImGui::Dummy({ 0, 10 });
        if (BigButton("FIND MATCH", { 280, 52 })) {
            static const char* m[] = { "ranked", "casual" };
            static const char* sd[] = { "any", "p1", "p2" };
            online->Queue(m[qMode], sd[qSide], kPings[qPing]);
        }
        EndCard();
    } else if (v.state == Online::QUEUED) {
        BeginCard("searching");
        const int secs = int((net::NowMs() - v.queueSinceMs) / 1000);
        std::snprintf(t, sizeof t, "SEARCHING%s", std::string(size_t(int(now * 2) % 4), '.').c_str());
        Text(F.heavy, 40.f, col::text, t);
        const char* side = v.queueSide == "p1" ? "1P side" : v.queueSide == "p2" ? "2P side" : "either side";
        std::snprintf(t, sizeof t, "%s  ·  %s  ·  %d:%02d", v.queueMode == "ranked" ? "Ranked" : "Casual", side, secs / 60, secs % 60);
        Text(F.bold, 15.f, col::gold, t);
        ImGui::Dummy({ 0, 10 });
        if (Button("Cancel", { 140, 34 })) online->Cancel();
        EndCard();
        ImGui::Dummy({ 0, 12 });
        PracticeQuickCard();
    } else if (v.state == Online::CHECKING || v.state == Online::READY) {
        BeginCard("found");
        Text(F.heavy, 40.f, col::text, v.state == Online::READY ? "MATCH READY" : "MATCH FOUND");
        std::snprintf(t, sizeof t, "vs %s (%d)   ·   you are %s", v.match.opponent.c_str(), v.match.opponentRating, v.match.side == "p1" ? "1P (left)" : "2P (right)");
        Text(F.bold, 16.f, col::gold, t);
        ImGui::Dummy({ 0, 2 });
        ConnectionBadge(v.match.opponentConn, v.state == Online::READY ? v.match.rtt : 0);
        ImGui::SameLine(0, 8);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
        {
            const char* conn = v.match.opponentConn == 1 ? "Wired" : v.match.opponentConn == 2 ? "Wi-Fi" : "Connection unknown";
            std::snprintf(t, sizeof t, "%s   ·   %s", v.match.opponentRegion.empty() ? "Region unknown" : v.match.opponentRegion.c_str(), conn);
            Text(F.body, 14.f, col::dim, t);
        }
        ImGui::Dummy({ 0, 4 });
        if (v.state == Online::CHECKING) {
            const int64_t leftMs = int64_t(v.acceptDeadlineMs) - int64_t(net::NowMs());
            const float frac = std::clamp(float(leftMs) / float(Online::kAcceptMs), 0.f, 1.f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, V(frac < 0.3f ? col::red : col::gold));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, V(col::cardHi));
            ImGui::ProgressBar(frac, { -1, 6 }, "");
            ImGui::PopStyleColor(2);
            ImGui::Dummy({ 0, 6 });
            if (!v.accepted) {
                // A / Cross or Enter accepts, B / Circle or Esc declines (TickAccept turned the client's own pad and
                // keyboard navigation off meanwhile, so the same presses don't also click around)
                const bool pad = !devices.empty();
                if (BigButton(pad ? "ACCEPT   (A / Cross)" : "ACCEPT   (Enter)", { 300, 52 }) ||
                    ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) || ImGui::IsKeyPressed(ImGuiKey_Enter, false) ||
                    ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
                    online->Accept();
                ImGui::SameLine(0, 12);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 9);
                if (Button(pad ? "Decline   (B / Circle)" : "Decline   (Esc)", { 200, 36 }) ||
                    ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) || ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    online->Decline();
                ImGui::Dummy({ 0, 4 });
                std::snprintf(t, sizeof t, "%d s to accept. Declining or letting it run out takes you out of the queue.",
                              int(std::max<int64_t>(0, (leftMs + 999) / 1000)));
                ImGui::PushFont(F.body, 13.f);
                Wrapped(col::faint, t);
                ImGui::PopFont();
            } else {
                std::snprintf(t, sizeof t, "Accepted. Waiting for %s...", v.match.opponent.c_str());
                Text(F.bold, 15.f, col::green, t);
            }
        } else {
            std::snprintf(t, sizeof t, "Connection: %d ms, %s", v.match.rtt, v.match.direct ? "direct" : "through the relay");
            Text(F.body, 14.f, col::green, t);
            ImGui::Dummy({ 0, 8 });
            ImGui::PushFont(F.body, 14.f);
            if (matchProc) {
                Text(F.bold, 15.f, col::text, "The game is starting.");
                if (StraightIn())
                    Wrapped(col::dim, "It goes straight to character select. Pick your characters there as usual.");
                else if (matchDirect) {
                    std::snprintf(t, sizeof t, "It goes straight into the fight, in about 15 seconds - keep your hands off the controller until "
                                  "it loads. You play %s on the %s side. First to 2 rounds, and nobody can pause. When it ends, "
                                  "the game's own menu comes up: if you both pick REMATCH you play again straight away; "
                                  "anything else ends the match.", matchTeam.c_str(), v.match.side == "p1" ? "left" : "right");
                    Wrapped(col::dim, t);
                } else {
                    Wrapped(col::dim, v.match.side == "p1"
                        ? "You host. The game goes through its menus by itself and opens a Player Match room; your opponent joins it by "
                          "themselves. You meet at character select - pick as usual. Rematches happen in the room."
                        : "Your opponent hosts. The game goes through its menus by itself and joins their Player Match room as soon as "
                          "it is open. You meet at character select - pick as usual. Rematches happen in the room.");
                    if (PairPlay()) Wrapped(col::dim, "Set up PAIR PLAY in the room, as before.");
                }
                ImGui::Dummy({ 0, 4 });
                if (setScore.Games() > 0) {
                    std::snprintf(t, sizeof t, "Score so far: you %d - %d %s", setScore.won, setScore.lost, v.match.opponent.c_str());
                    Text(F.bold, 15.f, col::gold, t);
                    ImGui::Dummy({ 0, 2 });
                }
                Wrapped(col::faint, matchDirect ? "The game closes by itself when the match is over, and the result comes from the game - nobody has to enter it."
                                                : "Play, then close the game. The result comes from the game itself - nobody has to enter it.");
            } else if (noResult) {
                if (setScore.Games() == 0)
                    Wrapped(col::amber, "The game closed without a finished match, so nothing was counted.");
                else {
                    std::snprintf(t, sizeof t, "The set ended level (%d - %d), so nothing was counted.", setScore.won, setScore.lost);
                    Wrapped(col::amber, t);
                }
                ImGui::Dummy({ 0, 4 });
                if (Button("Start the game again", { 200, 34 })) { noResult = false; matchStarted.clear(); }
            } else if (!matchNote.empty()) {
                Wrapped(col::amber, matchNote.c_str());
                ImGui::Dummy({ 0, 4 });
                if (Button("Try again", { 130, 34 })) { matchNote.clear(); matchStarted.clear(); }
            } else if (!installed) {
                Wrapped(col::amber, "Set up the game first (INSTALL page).");
            } else if (resultSent) {
                std::snprintf(t, sizeof t, "You %s %d - %d.", setScore.won > setScore.lost ? "won" : "lost", setScore.won, setScore.lost);
                Text(F.bold, 16.f, setScore.won > setScore.lost ? col::green : col::text, t);
                std::snprintf(t, sizeof t, "Sent to the server. It counts as soon as %s's game has closed too.", v.match.opponent.c_str());
                Wrapped(col::dim, t);
            } else {
                Wrapped(col::dim, "Starting the game...");
            }
            ImGui::PopFont();
        }
        ImGui::Dummy({ 0, 10 });
        if (!matchProc && Button(v.state == Online::READY ? "Back to lobby" : "Leave", { 150, 34 })) { online->Leave(); resultSent = noResult = false; }
        EndCard();
    }

    if (!v.board.empty()) {
        ImGui::Dummy({ 0, 12 });
        BeginCard("board", 0, "Leaderboard");
        if (ImGui::BeginTable("lb", 4, ImGuiTableFlags_RowBg)) {
            ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 40);
            ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
            ImGui::TableSetupColumn("rating", ImGuiTableColumnFlags_WidthFixed, 80);
            ImGui::TableSetupColumn("record", ImGuiTableColumnFlags_WidthFixed, 110);
            ImGui::PushStyleColor(ImGuiCol_TableRowBg, V(0));
            ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, V(Alpha(col::text, 0.025f)));
            for (size_t i = 0; i < v.board.size() && i < 20; ++i) {
                const BoardRow& r = v.board[i];
                const bool me = r.name == v.name;
                ImGui::TableNextRow(0, 28);
                ImGui::TableNextColumn();
                std::snprintf(t, sizeof t, "%d", r.rank);
                Text(F.heavy, 18.f, r.rank <= 3 ? col::gold : col::dim, t);
                ImGui::TableNextColumn();
                Text(F.bold, 14.f, me ? col::gold : col::text, r.name.c_str());
                ImGui::TableNextColumn();
                std::snprintf(t, sizeof t, "%d", r.rating);
                Text(F.bold, 14.f, col::text, t);
                ImGui::TableNextColumn();
                std::snprintf(t, sizeof t, "%d-%d", r.wins, r.games - r.wins);
                Text(F.body, 13.f, col::dim, t);
            }
            ImGui::PopStyleColor(2);
            ImGui::EndTable();
        }
        EndCard();
    } else if (v.state == Online::LOBBY) {
        ImGui::Dummy({ 0, 12 });
        BeginCard("board0", 0, "Leaderboard");
        Text(F.body, 13.5f, col::faint, "Players show up here after 5 ranked matches.");
        EndCard();
    }
}

// Stick or pad, for the input test's picture: what the player chose for this controller (Pad / Stick in the picture's
// corner), else a stick when SDL says so or the name says so (Victrix Pro FS, Hori Fighting Stick, Qanba, hitboxes...),
// else a pad.
bool App::Impl::ShowAsStick(const Device& dvc)
{
    const std::string chosen = client.Get("picture", dvc.guid, "");
    if (chosen == "stick") return true;
    if (chosen == "pad") return false;
    if (dvc.joy && SDL_GetJoystickType(dvc.joy) == SDL_JOYSTICK_TYPE_ARCADE_STICK) return true;
    std::string n = " " + dvc.name + " ";
    for (char& ch : n) ch = char(std::tolower(static_cast<unsigned char>(ch)));
    for (char& ch : n) if (!std::isalnum(static_cast<unsigned char>(ch))) ch = ' ';
    static const char* words[] = { " stick ", " fight ", " fighting ", " arcade ", " hitbox ", " hit box ", " snack box ", " fs ",
                                   " rap ", " panthera ", " kitsune ", " qanba ", " haute42 ", " mixbox ", " flatbox ", " lever ",
                                   " te2 ", " fightstick ", " arcadestick ", " leverless ", " obsidian ", " sanwa " };
    for (const char* w : words) if (n.find(w) != std::string::npos) return true;
    return false;
}

// What the input test lights: the output itself, or a macro that contains it (1+2 on RB lights 1 and 2).
bool App::Impl::PictureHeld(const Device& dvc, int out)
{
    if (OutHeld(dvc, out)) return true;
    struct M { int macro; int outs[4]; };
    static const M macros[] = { { O_M12, { O_X, O_Y, -1, -1 } }, { O_M34, { O_A, O_B, -1, -1 } }, { O_M13, { O_X, O_A, -1, -1 } },
                                { O_M24, { O_Y, O_B, -1, -1 } }, { O_M1234, { O_X, O_Y, O_A, O_B } }, { O_M134, { O_X, O_A, O_B, -1 } },
                                { O_M23, { O_Y, O_A, -1, -1 } }, { O_M14, { O_X, O_B, -1, -1 } } };
    for (const M& m : macros) {
        bool in = false;
        for (int o : m.outs) in |= o == out;
        if (in && OutHeld(dvc, m.macro)) return true;
    }
    return false;
}

// A pad, drawn like the one that is plugged in: an Xbox pad (stick over D-pad on the left, A B X Y in their colours), a
// PlayStation pad (D-pad over the sticks, touchpad, the four shapes) or a Nintendo one (Xbox-shaped, B A Y X, + / -).
// Each face button carries the game's number (1 2 3 4) in a small badge; everything lights while it is held.
void App::Impl::DrawPadPicture(Device* dv, ImVec2 p, float w, float height)
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    auto held = [&](int out) { return dv && PictureHeld(*dv, out); };
    const PadFamily fam = dv ? DeviceFamily(*dv) : FAM_XBOX;
    const bool ps = fam == FAM_PLAYSTATION, nin = fam == FAM_NINTENDO;
    const ImU32 off = IM_COL32(40, 40, 50, 255), body = IM_COL32(24, 24, 31, 255), dark = IM_COL32(6, 12, 24, 255);
    const float span = std::min(w * 0.86f, height * 2.45f);
    const float L = p.x + (w - span) * 0.5f, R = L + span;
    const float T = p.y + height * 0.25f, B = p.y + height * 0.98f, H = B - T;
    auto X = [&](float f) { return L + span * f; };
    auto Y = [&](float f) { return T + H * f; };

    // shoulders above the body: trigger outside, bumper inside (TAG is the game's LB)
    auto shoulder = [&](int out, float x0, float x1, float y0, float y1, const char* label) {
        const bool on = held(out);
        d->AddRectFilled({ x0, y0 }, { x1, y1 }, on ? col::red : off, 6.f);
        ImGui::PushFont(F.bold, std::max(10.f, height * 0.07f));
        const ImVec2 ts = ImGui::CalcTextSize(label);
        d->AddText({ (x0 + x1 - ts.x) * 0.5f, (y0 + y1 - ts.y) * 0.5f }, on ? dark : col::dim, label);
        ImGui::PopFont();
    };
    char lb[32];
    std::snprintf(lb, sizeof lb, "TAG %s", PadButtonName(fam, O_LB));
    const float sy0 = p.y + height * 0.03f, sy1 = p.y + height * 0.12f, by0 = p.y + height * 0.135f, by1 = p.y + height * 0.225f;
    shoulder(O_LT, X(0.10f), X(0.27f), sy0, sy1, PadButtonName(fam, O_LT));
    shoulder(O_RT, X(0.73f), X(0.90f), sy0, sy1, PadButtonName(fam, O_RT));
    shoulder(O_LB, X(0.08f), X(0.36f), by0, by1, lb);
    shoulder(O_RB, X(0.64f), X(0.92f), by0, by1, PadButtonName(fam, O_RB));

    // the body and its two grips
    d->AddRectFilled({ X(0.06f), Y(0.0f) }, { X(0.94f), Y(0.62f) }, body, H * 0.3f);
    d->AddCircleFilled({ X(0.17f), Y(0.68f) }, H * 0.3f, body, 40);
    d->AddCircleFilled({ X(0.83f), Y(0.68f) }, H * 0.3f, body, 40);
    d->AddRect({ X(0.06f), Y(0.0f) }, { X(0.94f), Y(0.62f) }, col::line, H * 0.3f, 0, 1.f);

    // sticks: both show where they really are (when SDL knows the pad); L3 / R3 light them
    const float sr = H * 0.16f;
    auto stick = [&](ImVec2 c, bool left) {
        d->AddCircleFilled(c, sr, IM_COL32(16, 16, 22, 255), 32);
        d->AddCircle(c, sr, col::line, 32, 1.5f);
        float dx = 0, dy = 0;
        if (dv && dv->pad) {
            dx = std::clamp(float(SDL_GetGamepadAxis(dv->pad, left ? SDL_GAMEPAD_AXIS_LEFTX : SDL_GAMEPAD_AXIS_RIGHTX)) / 32767.f, -1.f, 1.f);
            dy = std::clamp(float(SDL_GetGamepadAxis(dv->pad, left ? SDL_GAMEPAD_AXIS_LEFTY : SDL_GAMEPAD_AXIS_RIGHTY)) / 32767.f, -1.f, 1.f);
        }
        const bool moved = std::fabs(dx) > 0.25f || std::fabs(dy) > 0.25f;
        const bool click = held(left ? O_LS : O_RS);
        d->AddCircleFilled({ c.x + dx * sr * 0.45f, c.y + dy * sr * 0.45f }, sr * 0.62f, click ? col::gold : moved ? col::red : IM_COL32(58, 58, 70, 255), 32);
    };
    // the D-pad (lights with the game's directions: D-pad and stick alike)
    auto dpad = [&](ImVec2 c) {
        const float a = H * 0.06f;
        d->AddRectFilled({ c.x - a, c.y - a }, { c.x + a, c.y + a }, off);
        struct Arm { int out; float x, y; };
        const Arm arms[] = { { O_UP, 0, -1 }, { O_DOWN, 0, 1 }, { O_LEFT, -1, 0 }, { O_RIGHT, 1, 0 } };
        for (const Arm& arm : arms) {
            const ImVec2 c0 = { c.x + arm.x * a * 2.f, c.y + arm.y * a * 2.f };
            d->AddRectFilled({ c0.x - a, c0.y - a }, { c0.x + a, c0.y + a }, held(arm.out) ? col::red : off, 3.f);
        }
    };
    // the face buttons in this pad's own look, with the game's number beside each
    auto face = [&](ImVec2 c) {
        const float br = H * 0.095f, gap = br * 2.0f;
        struct Fb { int out; const char* num; float x, y; };
        const Fb fb[] = { { O_X, "1", -1, 0 }, { O_Y, "2", 0, -1 }, { O_A, "3", 0, 1 }, { O_B, "4", 1, 0 } };
        for (const Fb& f : fb) {
            const ImVec2 bc = { c.x + f.x * gap, c.y + f.y * gap };
            const bool on = held(f.out);
            d->AddCircleFilled(bc, br, on ? col::gold : IM_COL32(16, 16, 22, 255), 28);
            d->AddCircle(bc, br, on ? col::text : col::line, 28, 1.2f);
            const ImU32 ink = on ? dark : 0;
            const float g = br * 0.48f;
            if (ps) {
                // Square (west), Triangle (north), Cross (south), Circle (east) in their colours
                if (f.out == O_X) d->AddRect({ bc.x - g, bc.y - g }, { bc.x + g, bc.y + g }, ink ? ink : IM_COL32(230, 140, 200, 255), 0, 0, 2.f);
                if (f.out == O_Y) d->AddTriangle({ bc.x, bc.y - g * 1.1f }, { bc.x - g * 1.05f, bc.y + g * 0.75f }, { bc.x + g * 1.05f, bc.y + g * 0.75f }, ink ? ink : IM_COL32(80, 210, 190, 255), 2.f);
                if (f.out == O_A) { const ImU32 k = ink ? ink : IM_COL32(120, 160, 255, 255); d->AddLine({ bc.x - g, bc.y - g }, { bc.x + g, bc.y + g }, k, 2.f); d->AddLine({ bc.x - g, bc.y + g }, { bc.x + g, bc.y - g }, k, 2.f); }
                if (f.out == O_B) d->AddCircle(bc, g * 1.05f, ink ? ink : IM_COL32(240, 100, 100, 255), 24, 2.f);
            } else {
                // Xbox: X Y A B (blue, yellow, green, red); Nintendo: Y X B A by position
                const char* letter = nin ? (f.out == O_X ? "Y" : f.out == O_Y ? "X" : f.out == O_A ? "B" : "A")
                                         : (f.out == O_X ? "X" : f.out == O_Y ? "Y" : f.out == O_A ? "A" : "B");
                const ImU32 tint = nin ? col::text
                                 : f.out == O_X ? IM_COL32(90, 150, 255, 255) : f.out == O_Y ? IM_COL32(240, 200, 70, 255)
                                 : f.out == O_A ? IM_COL32(110, 210, 110, 255) : IM_COL32(240, 100, 100, 255);
                ImGui::PushFont(F.bold, br * 1.15f);
                const ImVec2 ts = ImGui::CalcTextSize(letter);
                d->AddText({ bc.x - ts.x * 0.5f, bc.y - ts.y * 0.5f }, ink ? ink : tint, letter);
                ImGui::PopFont();
            }
            // the game's number
            const ImVec2 nc = { bc.x + br * 0.85f, bc.y - br * 0.85f };
            d->AddCircleFilled(nc, br * 0.42f, on ? col::text : IM_COL32(60, 60, 74, 255), 16);
            ImGui::PushFont(F.heavy, br * 0.62f);
            const ImVec2 ns = ImGui::CalcTextSize(f.num);
            d->AddText({ nc.x - ns.x * 0.5f, nc.y - ns.y * 0.5f }, on ? dark : col::text, f.num);
            ImGui::PopFont();
        }
    };
    // Back / Start (Share / Options, - / +)
    auto menu = [&](ImVec2 c, int out) {
        d->AddRectFilled({ c.x - 9, c.y - 5 }, { c.x + 9, c.y + 5 }, held(out) ? col::gold : off, 5.f);
        ImGui::PushFont(F.bold, 10.f);
        const char* name = PadButtonName(fam, out);
        const ImVec2 ts = ImGui::CalcTextSize(name);
        d->AddText({ c.x - ts.x * 0.5f, c.y + 7 }, col::faint, name);
        ImGui::PopFont();
    };

    if (ps) {
        d->AddRectFilled({ X(0.37f), Y(0.06f) }, { X(0.63f), Y(0.36f) }, IM_COL32(20, 20, 27, 255), 6.f);   // touchpad
        d->AddRect({ X(0.37f), Y(0.06f) }, { X(0.63f), Y(0.36f) }, col::line, 6.f, 0, 1.f);
        dpad({ X(0.22f), Y(0.30f) });
        face({ X(0.78f), Y(0.30f) });
        stick({ X(0.36f), Y(0.62f) }, true);
        stick({ X(0.64f), Y(0.62f) }, false);
        menu({ X(0.31f), Y(0.10f) }, O_BACK);
        menu({ X(0.69f), Y(0.10f) }, O_START);
    } else {
        stick({ X(0.23f), Y(0.30f) }, true);
        dpad({ X(0.36f), Y(0.62f) });
        stick({ X(0.64f), Y(0.62f) }, false);
        face({ X(0.77f), Y(0.30f) });
        menu({ X(0.43f), Y(0.30f) }, O_BACK);
        menu({ X(0.57f), Y(0.30f) }, O_START);
        d->AddCircle({ X(0.5f), Y(0.12f) }, H * 0.06f, col::line, 24, 1.5f);   // the guide / home button
    }
}

void App::Impl::Visualizer(Device* dv, float height)
{
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float w = ImGui::GetContentRegionAvail().x;
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, { p.x + w, p.y + height }, IM_COL32(13, 13, 17, 255), 10.f);
    auto held = [&](int out) { return dv && PictureHeld(*dv, out); };
    const bool stick = !dv || ShowAsStick(*dv);
    // under the picture: switch it when a controller is guessed wrong (kept for that controller)
    auto kindSwitch = [&] {
        if (!dv) return;
        static const char* kinds[] = { "Pad", "Stick" };
        int k = stick ? 1 : 0;
        ImGui::Dummy({ 0, 4 });
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + w - 200);
        ImGui::AlignTextToFramePadding();
        Text(F.body, 12.5f, col::faint, "Picture");
        ImGui::SameLine(0, 8);
        ImGui::PushID("picturekind");
        if (Segmented("pk", &k, kinds, 2, 140)) { client.Set("picture", dv->guid, k ? "stick" : "pad"); SaveClient(); }
        ImGui::PopID();
    };
    if (!stick) {
        DrawPadPicture(dv, p, w, height);
        ImGui::Dummy({ w, height });
        kindSwitch();
        return;
    }
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
    kindSwitch();
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
        {
            const PadFamily fam = DeviceFamily(dvc);
            char other[96];
            std::snprintf(other, sizeof other, "Unknown layout (%04x:%04x): set its buttons below", SDL_GetJoystickVendor(dvc.joy), SDL_GetJoystickProduct(dvc.joy));
            const char* kind = fam == FAM_PLAYSTATION ? "PlayStation layout" : fam == FAM_NINTENDO ? "Nintendo layout" : fam == FAM_XBOX ? "Xbox layout" : other;
            d->AddText({ p.x + 12, p.y + 30 }, custom ? col::gold : col::dim, custom ? "Custom map" : kind);
        }
        ImGui::PopFont();
        ImGui::Dummy({ 0, 4 });
        ImGui::PopID();
    }
    EndCard();
    // (the hitbox / stick options keep their defaults: Advanced removed, 0.3.2)
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
    Visualizer(dv, dv && !ShowAsStick(*dv) ? 220.f : 170.f);
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
            } else if (dv && row.out < O_M12 && DeviceFamily(*dv) != FAM_OTHER) {
                Text(F.body, 13.5f, col::text, PadButtonName(DeviceFamily(*dv), row.out));
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
    Header("SETTINGS", "The game's settings, in one place. They are read when the game starts.");
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
        RowLabel("Client window", "F11 or Alt+Enter switches too. The game's own window mode is under Graphics below.", ctl);
        ImGui::PushID("clientfs");
        if (Segmented("fs", &v, fs, 2, ctl)) SetFullscreen(v == 1);
        ImGui::PopID();
        Divider();
        RowLabel("Game volume", "The game's own sound. Changes apply straight away, while the game is running too.", ctl);
        int vol = client.GetInt("client", "volume", 100);
        ImGui::SetNextItemWidth(ctl);
        if (ImGui::SliderInt("##vol", &vol, 0, 100, "%d%%")) { client.Set("client", "volume", std::to_string(vol)); liveDirty = true; }
        if (ImGui::IsItemDeactivatedAfterEdit()) { SaveClient(); WriteVoiceLive(); }
        Divider();
        static const char* onoff[] = { "On", "Off" };
        int ns = client.GetInt("client", "netstatus", 1) ? 0 : 1;
        RowLabel("Fight overlay", "Ping, rollback frames and input delay at the bottom of the screen, both players' MMR under the health bars and a MIC marker while you talk, in online fights.", ctl);
        ImGui::PushID("netstatus");
        if (Segmented("ns", &ns, onoff, 2, ctl)) { client.Set("client", "netstatus", ns == 0 ? "1" : "0"); SaveClient(); }
        ImGui::PopID();
        Divider();
        int ua = client.GetInt("client", "unlockall", 1) ? 0 : 1;
        static const char* ul[] = { "Everything unlocked", "As saved" };
        RowLabel("Unlocks", "Every character, costume, item and stage open, to customise your characters in the game.", ctl);
        ImGui::PushID("unlockall");
        if (Segmented("ua", &ua, ul, 2, ctl)) { client.Set("client", "unlockall", ua == 0 ? "1" : "0"); SaveClient(); }
        ImGui::PopID();
    }
    EndCard();
    ImGui::Dummy({ 0, 12 });

    BeginCard("voice", 0, "Voice chat");
    {
        // Given to the game as a match starts, and written to the live file the game re-reads: every change here
        // applies straight away, mid-match too. Off: the microphone is never opened and the opponent is not heard.
        if (SDL_GetTicks() / 1000.0 - audioListAt > 5.0) RefreshAudio();
        static const char* modes[] = { "Off", "Push to talk", "Open mic" };
        static const char* modeKeys[] = { "off", "ptt", "open" };
        const std::string cur = client.Get("voice", "mode", "ptt");
        int m = cur == "off" ? 0 : cur == "open" ? 2 : 1;
        RowLabel("Voice chat", "Talk to your opponent during matches. Off turns it off completely: no microphone, no voice heard.", ctl);
        ImGui::PushID("vmode");
        if (Segmented("vmode", &m, modes, 3, ctl)) { client.Set("voice", "mode", modeKeys[m]); SaveClient(); WriteVoiceLive(); }
        ImGui::PopID();
        if (m != 0) {
            if (m == 1) {
                Divider();
                KeyBindRow("key", "Push-to-talk key", "Hold it while the game is in front to talk. Click the box, then press any key "
                                                     "or mouse button (not left or right click). Esc cancels.", 0x14);
            } else {
                Divider();
                KeyBindRow("mute_key", "Mute key", "Press it in the game to mute or unmute your microphone; MUTED shows in the corner "
                                                   "meanwhile. Click the box, then press a key or mouse button. Esc cancels.", 0);
            }
            Divider();
            RowLabel("Microphone", "The one you talk into. Windows default follows Windows' communications microphone.", ctl);
            AudioCombo("##micdev", "mic_device", true, ctl);
            Divider();
            RowLabel("Hear your opponent on", "Headset, speakers... Windows default follows Windows' sound output.", ctl);
            AudioCombo("##outdev", "out_device", false, ctl);
            Divider();
            RowLabel("Opponent's voice", "How loud your opponent is. 0% mutes them.", ctl);
            int hv = client.GetInt("voice", "volume", 100);
            ImGui::SetNextItemWidth(ctl);
            if (ImGui::SliderInt("##vvol", &hv, 0, 200, "%d%%")) { client.Set("voice", "volume", std::to_string(hv)); liveDirty = true; }
            if (ImGui::IsItemDeactivatedAfterEdit()) { SaveClient(); WriteVoiceLive(); }
            Divider();
            RowLabel("Your microphone", "How loud you are for your opponent.", ctl);
            int mv = client.GetInt("voice", "mic", 100);
            ImGui::SetNextItemWidth(ctl);
            if (ImGui::SliderInt("##mvol", &mv, 0, 200, "%d%%")) { client.Set("voice", "mic", std::to_string(mv)); liveDirty = true; }
            if (ImGui::IsItemDeactivatedAfterEdit()) { SaveClient(); WriteVoiceLive(); }
        }
        ImGui::Dummy({ 0, 4 });
        ImGui::PushFont(F.body, 13.f);
        Wrapped(col::faint, "Changes apply straight away, even mid-match. If voice chat was Off when the match started, turning it on "
                            "takes effect from the next match.");
        ImGui::PopFont();
    }
    EndCard();
    ImGui::Dummy({ 0, 12 });

    BeginCard("latency", 0, "Input and latency");
    {
        static const char* l[] = { "Removed", "Original" };
        RowLabel("Offline input delay", "The original game holds every press for 2 extra frames offline. Removed: about 33 ms faster.", ctl);
        seg("Game", "InputDelay", 0, l, 2);
        ImGui::Dummy({ 0, 8 });
        if (Button("Lowest input lag", { 170, 32 })) {
            game.SetInt("Game", "InputDelay", 0);
            game.SetInt("Graphics", "VSync", 0);
            game.SetInt("Graphics", "Tearing", 1);
            game.SetInt("Online", "Netcode", 1);
            SaveGameSettings();
        }
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        Text(F.body, 13.f, col::faint, "Delay removed, VSync off (VRR on), netcode Prioritize Response.");
    }
    EndCard();
    ImGui::Dummy({ 0, 12 });

    BeginCard("online", 0, "Online netcode");
    {
        // Standard or Prioritise response only (the user, 2026-11-23); an old Graphics / Custom choice counts as Standard
        static const char* n[] = { "Standard", "Prioritize response" };
        if (game.GetInt("Online", "Netcode", 0) > 1) { game.SetInt("Online", "Netcode", 0); SaveGameSettings(); }
        RowLabel("Rollback balance", "Prioritize response: one frame less delay, a little more rollback. "
                                     "The host's choice decides the match.", ctl);
        seg("Online", "Netcode", 0, n, 2);
        if (false) {
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

    BeginCard("graphics", 0, "Graphics");
    {
        // presets: render scale, anti-aliasing, texture filtering
        struct Preset { const char* name; int scale, aa, aniso; };
        static const Preset presets[] = { { "Performance", 1, 0, 0 }, { "Original", 1, 1, -1 }, { "High 1440p", 2, 1, 4 },
                                          { "Ultra 4K", 3, 2, 5 } };
        const int scale = std::clamp(game.GetInt("Graphics", "Scale", 2), 1, 3), aa = game.GetInt("Graphics", "AA", 1),
                  aniso = game.GetInt("Graphics", "Aniso", 4);
        int cur = 4;
        for (int i = 0; i < 4; ++i)
            if (presets[i].scale == scale && presets[i].aa == aa && presets[i].aniso == aniso) cur = i;
        RowLabel("Quality", "Performance: the lightest, for older PCs and laptops (720p, no smoothing). Original: the Xbox 360's "
                            "own 720p picture, smoothed. High: drawn at 1440p, sharp on most screens. Ultra: drawn at 4K with "
                            "the strongest smoothing and texture filtering. Custom: your own mix below.", ctl);
        static const char* pn[] = { "Performance", "Original", "High 1440p", "Ultra 4K", "Custom" };
        int pick = cur;
        ImGui::PushID("preset");
        if (Segmented("preset", &pick, pn, 5, ctl) && pick < 4) {
            game.SetInt("Graphics", "Scale", presets[pick].scale);
            game.SetInt("Graphics", "AA", presets[pick].aa);
            game.SetInt("Graphics", "Aniso", presets[pick].aniso);
            SaveGameSettings();
        }
        ImGui::PopID();
        Divider();
        static const char* sc[] = { "1x  720p", "2x  1440p", "3x  4K" };
        static const int scv[] = { 1, 2, 3 };
        RowLabel("Render resolution", "How many pixels the game is drawn with, as a multiple of the original 1280x720.", ctl);
        seg("Graphics", "Scale", 2, sc, 3, scv);
        Divider();
        static const char* aan[] = { "Off", "FXAA", "FXAA extreme" };
        RowLabel("Anti-aliasing", "Smooths jagged edges. Costs very little.", ctl);
        seg("Graphics", "AA", 1, aan, 3);
        Divider();
        static const char* an[] = { "Off", "Game's", "4x", "8x", "16x" };
        static const int anv[] = { 0, -1, 3, 4, 5 };
        RowLabel("Texture filtering", "Sharper floors and walls at an angle (anisotropic filtering).", ctl);
        seg("Graphics", "Aniso", 4, an, 5, anv);
        Divider();
        static const char* modes[] = { "Window", "Borderless", "Full screen" };
        RowLabel("Window mode", "Full screen takes the screen over (exclusive); borderless switches windows faster.", ctl);
        seg("Graphics", "Mode", 1, modes, 3);
        Divider();
        {
            // SDL's own display order, as the game counts them (0 = Windows' choice)
            int n = 0;
            SDL_DisplayID* ids = SDL_GetDisplays(&n);
            const int want = game.GetInt("Graphics", "Monitor", 0);
            std::string shown = "Windows' choice";
            if (want > 0 && want <= n) { const char* nm = SDL_GetDisplayName(ids[want - 1]); shown = std::to_string(want) + ": " + (nm ? nm : "Display"); }
            else if (want > 0) shown = "Not connected now - Windows' choice";
            RowLabel("Monitor", "The screen the game opens on.", ctl);
            ImGui::SetNextItemWidth(ctl);
            ImGui::PushFont(F.bold, 14.f);
            if (ImGui::BeginCombo("##monitor", shown.c_str())) {
                if (ImGui::Selectable("Windows' choice", want == 0)) { game.SetInt("Graphics", "Monitor", 0); SaveGameSettings(); }
                for (int i = 0; i < n; ++i) {
                    const char* nm = SDL_GetDisplayName(ids[i]);
                    const std::string label = std::to_string(i + 1) + ": " + (nm ? nm : "Display");
                    if (ImGui::Selectable(label.c_str(), want == i + 1)) { game.SetInt("Graphics", "Monitor", i + 1); SaveGameSettings(); }
                }
                ImGui::EndCombo();
            }
            ImGui::PopFont();
            SDL_free(ids);
        }
        Divider();
        static const char* onoff[] = { "On", "Off" };
        static const int onoffv[] = { 1, 0 };
        RowLabel("VSync", "Off can lower delay a little but may tear the picture.", ctl);
        seg("Graphics", "VSync", 1, onoff, 2, onoffv);
        Divider();
        RowLabel("Variable refresh (G-Sync / FreeSync)", "Lets a VRR screen follow the game; also needed for VSync Off without stutter.", ctl);
        seg("Graphics", "Tearing", 1, onoff, 2, onoffv);
        Divider();
        static const char* asp[] = { "16:9 (bars)", "Stretch" };
        RowLabel("Aspect ratio", "16:9 keeps the original shape on any screen.", ctl);
        seg("Graphics", "Stretch", 0, asp, 2);
        Divider();
        static const char* shc[] = { "In the background", "Before drawing" };
        RowLabel("Shader compiling", "The game's shaders are compiled on this PC the first time each one is seen, then kept. "
                                     "In the background: no stutter, but an effect can be missing for a moment the first time. "
                                     "Before drawing: everything right from the first frame, with short hitches early on.", ctl);
        seg("Graphics", "SyncShaders", 0, shc, 2);
        Divider();
        static const char* api[] = { "Direct3D 12", "Vulkan" };
        RowLabel("Renderer", "Direct3D 12 is the tested one; try Vulkan if the picture has problems.", ctl);
        seg("Graphics", "Vulkan", 0, api, 2);
        Divider();
        RowLabel("Frame rate", nullptr, ctl);
        ImGui::AlignTextToFramePadding();
        Text(F.bold, 14.f, col::text, "60 fps, locked (the game's speed)");
        ImGui::Dummy({ 0, 4 });
        ImGui::PushFont(F.body, 13.f);
        Wrapped(col::faint, "Graphics changes take effect the next time the game starts.");
        ImGui::PopFont();
    }
    EndCard();
}

void App::Impl::PageInstall()
{
    Header("INSTALL", "Build the PC version from your own Xbox 360 disc. The game build comes with the client; your disc supplies the game.");
    builder.Poll();
    PollDialogs();
    const bool busy = builder.Running();

    if (installed) {
        BeginCard("current", 0, "Installed");
        Text(F.bold, 15.f, col::text, installDir.c_str());
        std::string v = "Game build " + client.Get("client", "installed_tag", "unknown") + (layerOutdated ? "  ·  this client has a newer one" : "");
        Text(F.body, 13.5f, col::dim, v.c_str());
        ImGui::Dummy({ 0, 6 });
        if (Button(layerOutdated ? "Update game build" : "Reinstall game build", { 220, 32 }, !busy && !gameRunning)) {
            BuildRequest r;
            r.dest = installDir;
            r.onlyLayer = true;
            builder.Start(r);
        }

        ImGui::SameLine();
        if (Button("Open folder", { 130, 32 })) plat::OpenFolder(installDir);
        if (gameRunning) { ImGui::Dummy({ 0, 2 }); Text(F.body, 13.f, col::amber, "Close the game to change its files."); }
        EndCard();
        ImGui::Dummy({ 0, 12 });
    }

    BeginCard("build", 0, installed ? "Rebuild or update" : "Build the game");
    ImGui::PushFont(F.body, 14.f);
    Wrapped(col::dim, "You need your own Tekken Tag Tournament 2 (USA, Europe) Xbox 360 disc image and its Title Update 3. "
                      "No game files come with the client; the game's files are copied out of your image on this PC. About 8 GB.");
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
    const uint64_t free = plat::FreeBytes(dest);
    if (free) {
        char t[96];
        std::snprintf(t, sizeof t, "%.0f GB free on that drive", double(free) / double(1ull << 30));
        Text(F.body, 12.5f, free < (9ull << 30) ? col::red : col::faint, t);
    }
    ImGui::Dummy({ 0, 8 });
    const bool ready = ((!iso.empty() && !tu.empty()) || Builder::Extracted(dest)) && !dest.empty() && !busy && !gameRunning;
    if (BigButton(busy ? "BUILDING..." : installed ? "REBUILD" : "BUILD GAME", { 260, 52 }, ready)) {
        BuildRequest r{ iso, tu, dest, {}, false };
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
            CheckLayer();
        }
    }
    ImGui::Dummy({ 0, 12 });
    BeginCard("clientver", 0, "This client");
    {
        std::string v = std::string("Version ") + TTT2_CLIENT_VERSION;
        const Updater::State st = updater.GetState();
        if (st == Updater::UP_TO_DATE) v += "  ·  up to date";
        else if (st == Updater::AVAILABLE || st == Updater::READY) v += "  ·  " + updater.Latest() + " available (see the top of the page)";
        else if (st == Updater::FAILED) v += "  ·  " + updater.Error();
        Text(F.body, 14.f, col::text, v.c_str());
        ImGui::Dummy({ 0, 4 });
        if (Button(st == Updater::CHECKING ? "Checking..." : "Check for updates", { 170, 32 }, st != Updater::CHECKING && st != Updater::DOWNLOADING))
            updater.Check();
        ImGui::SameLine();
        if (Button("Releases page", { 140, 32 })) plat::OpenUrl(std::string("https://github.com/") + Updater::kRepo + "/releases");
    }
    EndCard();
    ImGui::Dummy({ 0, 10 });
    ImGui::PushFont(F.body, 12.5f);
    Wrapped(col::faint, "This client is not affiliated with Bandai Namco or Microsoft. You must own the game.");
    ImGui::PopFont();
}

// The game build ships in the client's "game build" folder; the installed copy is brought up to date before each start.
bool App::Impl::EnsureOnlineExe(std::string& note)
{
    if (!installed) { note = "Set up the game first (Install page)."; return false; }
    const std::string engine = Builder::EngineDir();
    if (!plat::FileExists(plat::JoinPath(engine, plat::kGameExe)) || plat::JoinPath(engine, "x") == plat::JoinPath(installDir, "x"))
        return true;   // nothing to bring
    std::string a, b, err;
    for (const std::string& f : plat::ListFilesRecursive(engine)) {
        const std::string rel = f.substr(engine.size() + 1), to = plat::JoinPath(installDir, rel);
        if (plat::ReadBinary(f, a) && plat::ReadBinary(to, b) && a == b) continue;
        const size_t slash = to.find_last_of("\\/");
        if (slash != std::string::npos) plat::MakeDir(to.substr(0, slash));
        if (!plat::CopyFileTo(f, to, err)) { note = "Could not update " + rel + " in the game folder: " + err; return false; }
    }
    return true;
}

// What every start of the online build gets: volume, voice chat, the connection status, unlocks.
std::vector<std::pair<std::string, std::string>> App::Impl::CommonEnv()
{
    std::vector<std::pair<std::string, std::string>> env = { { "TTT2_MASTER_VOLUME", std::to_string(client.GetInt("client", "volume", 100)) },
             { "TTT2_VOICE", client.Get("voice", "mode", "ptt") },
             { "TTT2_VOICE_KEY", std::to_string(client.GetInt("voice", "key", 0x14)) },
             { "TTT2_VOICE_VOLUME", std::to_string(client.GetInt("voice", "volume", 100)) },
             { "TTT2_MIC_VOLUME", std::to_string(client.GetInt("voice", "mic", 100)) },
             { "TTT2_MUTE_KEY", std::to_string(client.GetInt("voice", "mute_key", 0)) },
             { "TTT2_MIC_DEVICE", client.Get("voice", "mic_device", "") },
             { "TTT2_VOICE_DEVICE", client.Get("voice", "out_device", "") },
             { "TTT2_VOICE_LIVE", (WriteVoiceLive(), plat::JoinPath(plat::ClientDataDir(), "voice_live.ini")) },
             { "TTT2_PROMPT", plat::JoinPath(plat::ClientDataDir(), "match_prompt.ini") },
             { "TTT2_NET_STATUS", client.GetInt("client", "netstatus", 1) ? "1" : "0" },
             { "TTT2_UNLOCK_ALL", client.GetInt("client", "unlockall", 1) ? "1" : "0" },
             { "TTT2_RANKS", RanksFile() },
             { "TTT2_SERVER_RANKS", serverRanks ? ServerRanksFile() : std::string() },
             { "TTT2_INPUT_DELAY", game.GetInt("Game", "InputDelay", 0) ? "2" : "0" },
             { "REX_TITLE_GAME_ROOT", plat::JoinPath(installDir, "game") },
             { "REX_TITLE_UPDATE_ROOT", plat::JoinPath(installDir, "update") } };
    const auto g = GraphicsEnv();
    env.insert(env.end(), g.begin(), g.end());
    return env;
}

// The Graphics settings as the game build's own setting names (REX_ + the name, dots as underscores).
// The frame rate is not here: the game is locked at 60.
std::vector<std::pair<std::string, std::string>> App::Impl::GraphicsEnv()
{
    const int scale = std::clamp(game.GetInt("Graphics", "Scale", 2), 1, 3);
    const int aa = std::clamp(game.GetInt("Graphics", "AA", 1), 0, 2);
    const int aniso = std::clamp(game.GetInt("Graphics", "Aniso", 4), -1, 5);
    const int mode = std::clamp(game.GetInt("Graphics", "Mode", 1), 0, 2);
    static const char* aaName[] = { "none", "fxaa", "fxaa_extreme" };
    return { { "REX_GPU_RESOLUTION_SCALE_X", std::to_string(scale) },
             { "REX_GPU_RESOLUTION_SCALE_Y", std::to_string(scale) },
             { "REX_GPU_XENOS_SWAP_POST_EFFECT", aaName[aa] },
             { "REX_GPU_ANISOTROPIC_OVERRIDE", std::to_string(aniso) },
             { "REX_UI_FULLSCREEN", mode == 0 ? "false" : "true" },
             { "REX_UI_FULLSCREEN_EXCLUSIVE", mode == 2 ? "true" : "false" },
             { "REX_UI_MONITOR", std::to_string(std::clamp(game.GetInt("Graphics", "Monitor", 0), 0, 16)) },
             { "REX_GPU_VSYNC", game.GetInt("Graphics", "VSync", 1) ? "true" : "false" },
             { "REX_GPU_XENOS_PRESENT_LETTERBOX", game.GetInt("Graphics", "Stretch", 0) ? "false" : "true" },
             { "REX_GPU_D3D12_ALLOW_TEARING", game.GetInt("Graphics", "Tearing", 1) ? "true" : "false" },
             { "REX_GPU_DRIVER", game.GetInt("Graphics", "Vulkan", 0) ? "vulkan" : "d3d12" },
             { "REX_GPU_XENOS_ASYNC_SHADER_COMPILATION", game.GetInt("Graphics", "SyncShaders", 0) ? "false" : "true" } };
}

// The server's ranks: into the file the game reads at its start, and in place of the save's on the ONLINE page; a rated
// match's changes as the server worked them out.
void App::Impl::TakeServerRanks()
{
    if (!online) return;
    const Online::View v = online->Snapshot();
    if (v.charsVersion != serverCharsVersion && v.chars.size() >= 59) {
        serverCharsVersion = v.charsVersion;
        std::string text;
        char line[96];
        for (size_t c = 0; c < v.chars.size(); ++c) {
            const Online::View::Rank& r = v.chars[c];
            std::snprintf(line, sizeof line, "c=%zu rank=%d points=%d streak=%d bar1=%d bar2=%d\r\n", c, r.rank, r.points, r.streak, r.bar1, r.bar2);
            text += line;
            if (c < 64) gameRank[c] = r.rank;
        }
        serverRanks = plat::WriteText(ServerRanksFile(), text);
        ranksKnown = true;
    }
    if (!v.lastMatch.empty() && v.lastMatch != serverRanksFor) {
        serverRanksFor = v.lastMatch;
        lastRanks.clear();
        for (const Online::View::RankChange& r : v.lastRanks) lastRanks.push_back({ r.c, r.before, r.after, r.pointsBefore, r.pointsAfter });
    }
}

// This match's fights as the game wrote them ("game=1 result=won winner=0 mine=0 frame=812 teams=14,42,0,0"), for
// the server: it works the ranked results out from them, and counts them only when both PCs' are alike.
std::string App::Impl::FightsJson()
{
    std::string text, out = "[";
    if (!plat::ReadBinary(ResultsFile(), text)) return "[]";
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) break;
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        int winner = -1, a = -1, b = -1, c = -1, d = -1;
        const size_t w = line.find("winner="), tm = line.find("teams=");
        if (w == std::string::npos || tm == std::string::npos) continue;
        if (std::sscanf(line.c_str() + w, "winner=%d", &winner) != 1 || std::sscanf(line.c_str() + tm, "teams=%d,%d,%d,%d", &a, &b, &c, &d) != 4) continue;
        char f[128];
        std::snprintf(f, sizeof f, "%s{\"teams\":[[%d,%d],[%d,%d]],\"winner\":%d,\"draw\":%s}", out.size() > 1 ? "," : "", a, b, c, d, winner,
                      line.find("result=draw") != std::string::npos ? "true" : "false");
        out += f;
    }
    return out + "]";
}

// Every character's rank as the game last wrote it ("c=14 rank=10 points=1500" a line).
void App::Impl::ReadRanks()
{
    ranksReadAt = SDL_GetTicks() / 1000.0;
    if (serverRanks) return;   // the server's are the ones that count
    std::string text;
    if (!plat::ReadBinary(RanksFile(), text)) return;
    int seen = 0;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) end = text.size();
        int c = -1, rank = -1, points = 0;
        if (std::sscanf(text.substr(at, end - at).c_str(), "c=%d rank=%d points=%d", &c, &rank, &points) >= 2 && c >= 0 && c < 64 && rank >= 0 && rank <= 42) {
            gameRank[c] = rank;
            ++seen;
        }
        at = end + 1;
    }
    if (seen) ranksKnown = true;
}

// What the last game's ranked match did to the characters played (the result file's "rank" lines; none for a casual one).
std::vector<App::Impl::RankChange> App::Impl::ReadRankChanges()
{
    std::vector<RankChange> out;
    std::string text;
    if (!plat::ReadBinary(ResultsFile(), text)) return out;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) break;
        RankChange r;
        if (std::sscanf(text.substr(at, end - at).c_str(), "rank c=%d before=%d after=%d pb=%d pa=%d", &r.character, &r.before, &r.after,
                        &r.pointsBefore, &r.pointsAfter) >= 3) {
            // one line per character: a later game of the same match replaces the earlier line's "after"
            bool merged = false;
            for (RankChange& have : out)
                if (have.character == r.character) { have.after = r.after; have.pointsAfter = r.pointsAfter; merged = true; }
            if (!merged) out.push_back(r);
        }
        at = end + 1;
    }
    return out;
}

bool App::Impl::StartMatch(const MatchInfo& m)
{
    matchStarted = m.id;
    std::string err;
    if (practiceProc) {   // practice while searching: the match takes over
        plat::KillProcess(practiceProc);
        practiceProc = 0;
    }
    if (!EnsureOnlineExe(matchNote)) return false;
    const int port = online->HandOffUdp();
    const std::string file = plat::JoinPath(plat::ClientDataDir(), "match.ini");
    std::string ini = "id=" + m.id + "\nslot=" + (m.side == "p1" ? "0" : "1") + "\npeer=" + m.peerAddr + "\nrelay=" + m.relayAddr +
                      "\ntoken=" + m.relayToken + "\nlocalport=" + std::to_string(port) + "\ndirect=" + (m.direct ? "1" : "0") + "\n";
    if (!plat::WriteText(file, ini)) { online->ReclaimUdp(); matchNote = "Could not write the match file."; return false; }
    const Online::View ov = online->Snapshot();
    auto env = CommonEnv();
    env.insert(env.end(), { { "TTT2_NET", "ttt2net" }, { "TTT2_MATCH", file }, { "TTT2_ROLLBACK", "1" },
                            { "TTT2_RESULT", ResultsFile() } });
    // Rollback balance: Standard holds this PC's keys back up to 2 frames, Prioritize response up to 1 (the rest is guessed)
    env.push_back({ "TTT2_RB_MAXDELAY", game.GetInt("Online", "Netcode", 0) == 1 ? "1" : "2" });
    plat::MakeDir(ReplaysDir());
    env.push_back({ "TTT2_REPLAY_DIR", ReplaysDir() });   // the game saves each fight there (the PLAY page's Replays)
    // the games of this match as the game decides them; a game started again for the same match adds to them
    if (resultsFor != m.id) { plat::DeleteFileAt(ResultsFile()); resultsFor = m.id; }
    setScore = ReadScore();
    resultSent = noResult = false;
    // The game's own online room (TTT2_BOOT=online): the game goes through its own menus by itself - player 1
    // creates a Player Match session, player 2 finds it (the only one it can see) and joins, both are made ready -
    // and the two players meet at its character select. Rematches are the room's own.
    //
    // Straight into the fight (TTT2_BOOT=versus, the default): no online menus at all - both games start the same
    // offline versus fight by themselves (each player's team from here, the same stage) and share their inputs from
    // its first frame. One match, first to 2 rounds; the game closes when it is over. A Random pick is made here,
    // because both games have to be told the same characters.
    matchDirect = !RoomStart();
    matchTeam.clear();
    env.insert(env.end(), { { "TTT2_NAME", ov.name }, { "TTT2_OPP_NAME", m.opponent } });
    {
        const bool solo = client.Get("picks", "team", "duo") == "solo";
        int c1 = client.GetInt("picks", "c1", 14), c2 = solo ? c1 : client.GetInt("picks", "c2", 42);
        if (matchDirect) {
            const int count = int(sizeof roster::kCharacters / sizeof roster::kCharacters[0]);
            std::srand(unsigned(std::time(nullptr)) ^ unsigned(net::NowMs()));
            if (c1 == roster::kRandom) c1 = roster::kCharacters[std::rand() % count].id;
            if (solo) c2 = c1;
            for (int tries = 0; (c2 == roster::kRandom || (!solo && c2 == c1)) && tries < 50; ++tries) c2 = roster::kCharacters[std::rand() % count].id;
            matchTeam = solo ? std::string(roster::CharacterName(c1))
                             : std::string(roster::CharacterName(c1)) + " and " + roster::CharacterName(c2);
            env.insert(env.end(), { { "TTT2_BOOT", "versus" }, { "TTT2_FIGHTERS", std::to_string(c1) + "," + std::to_string(c2) } });
        } else {
            // the room: the game picks them at each character select by itself (a Random pick is the game's own Random cell)
            const std::string f = std::to_string(c1) + "," + std::to_string(c2);
            env.insert(env.end(), { { "TTT2_BOOT", "online" }, { "TTT2_MATCH_KIND", m.mode == "ranked" ? "ranked" : "casual" },
                                    { "TTT2_FIGHTERS", f + "," + f } });
        }
    }
    env.insert(env.end(), { { "TTT2_MMR", std::to_string(ov.rating) }, { "TTT2_OPP_MMR", std::to_string(m.opponentRating) },
                            });
    // test builds: the game's step-by-step trace goes into the test logs under this match's start time; the
    // "straight in" start, which is still being worked on, also logs every packet of the game's sockets
    {
        char stamp[40] = "match";
        const std::time_t now = std::time(nullptr);
        if (const std::tm* tm = std::localtime(&now)) std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M.%S", tm);
        logStamp = stamp;
    }
    plat::WriteText(plat::JoinPath(TestLogsDir(), "what to send.txt"), kTestLogsNote);   // makes the folder too
    PruneTestLogs();
    env.push_back({ "TTT2_TRACE_FILE", plat::JoinPath(plat::GameDataDir(), "trace.txt") });   // into test log.txt when the game closes
    if (StraightIn()) env.push_back({ "TTT2_NET_TRACE", "1" });
    matchProc = plat::LaunchWithEnv(installDir, plat::kGameExe, env, err);
    if (!matchProc) { online->ReclaimUdp(); matchNote = "Could not start the game: " + err; return false; }
    gameRunning = true;
    matchNote.clear();
    resultFor = m;
    return true;
}

// A character list (`key` in the client's [picks] / [practice]); true when it changed.
bool App::Impl::CharacterCombo(const char* id, const char* key, bool allowRandom)
{
    const char* sec = std::strncmp(key, "o", 1) == 0 || std::strcmp(key, "stage") == 0 ? "practice" : "picks";
    const int cur = client.GetInt(sec, key, roster::kRandom);
    bool changed = false;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(205);
    if (ImGui::BeginCombo("##c", roster::CharacterName(cur), ImGuiComboFlags_HeightLarge)) {
        if (allowRandom && ImGui::Selectable("Random", cur == roster::kRandom)) { client.Set(sec, key, std::to_string(roster::kRandom)); changed = true; }
        for (const roster::Character& c : roster::kCharacters)
            if (ImGui::Selectable(c.name, c.id == cur)) { client.Set(sec, key, std::to_string(c.id)); changed = true; }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    if (changed) SaveClient();
    return changed;
}

// ------------------------------------------------------------------ voice chat helpers

// The settings the running game follows (voice_chat.h, TTT2_VOICE_LIVE): rewritten whole on every change.
void App::Impl::WriteVoiceLive()
{
    std::string t = "mode=" + client.Get("voice", "mode", "ptt") + "\nkey=" + std::to_string(client.GetInt("voice", "key", 0x14)) +
                    "\nmute_key=" + std::to_string(client.GetInt("voice", "mute_key", 0)) +
                    "\nvolume=" + std::to_string(client.GetInt("voice", "volume", 100)) +
                    "\nmic=" + std::to_string(client.GetInt("voice", "mic", 100)) +
                    "\nmic_device=" + client.Get("voice", "mic_device", "") + "\nout_device=" + client.Get("voice", "out_device", "") +
                    "\ngame_volume=" + std::to_string(client.GetInt("client", "volume", 100)) +
                    "\nend=1\n";
    plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "voice_live.ini"), t);
    liveDirty = false;
    liveWrittenAt = SDL_GetTicks() / 1000.0;
}

void App::Impl::RefreshAudio()
{
    mics = plat::AudioDevices(true);
    outs = plat::AudioDevices(false);
    defMic = plat::DefaultAudioDevice(true);
    defOut = plat::DefaultAudioDevice(false);
    audioListAt = SDL_GetTicks() / 1000.0;
}

// "Windows default (its name)" first, then every device that is plugged in. A chosen device that is unplugged stays
// chosen (it comes back when it is plugged in again); meanwhile the game uses the default.
bool App::Impl::AudioCombo(const char* id, const char* key, bool microphone, float width)
{
    const std::vector<plat::AudioDevice>& list = microphone ? mics : outs;
    const std::string& def = microphone ? defMic : defOut;
    const std::string cur = client.Get("voice", key, "");
    const std::string nameKey = std::string(key) + "_name";
    std::string curName = client.Get("voice", nameKey, ""), defName;
    bool present = false;
    for (const plat::AudioDevice& d : list) {
        if (d.id == cur) { present = true; curName = d.name; }
        if (d.id == def) defName = d.name;
    }
    const std::string defaultLabel = defName.empty() ? std::string("Windows default") : "Windows default (" + defName + ")";
    std::string shown = cur.empty() ? defaultLabel
                      : present     ? curName
                                    : (curName.empty() ? std::string("A device") : curName) + " (unplugged: using Windows default)";
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    if (ImGui::BeginCombo(id, shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) RefreshAudio();
        if (ImGui::Selectable(defaultLabel.c_str(), cur.empty())) {
            client.Set("voice", key, "");
            client.Set("voice", nameKey, "");
            changed = true;
        }
        for (const plat::AudioDevice& d : list) {
            const std::string label = d.name + "##" + d.id;
            if (ImGui::Selectable(label.c_str(), d.id == cur)) {
                client.Set("voice", key, d.id);
                client.Set("voice", nameKey, d.name);
                changed = true;
            }
        }
        if (list.empty()) Text(F.body, 13.f, col::faint, microphone ? "No microphone found." : "No sound output found.");
        ImGui::EndCombo();
    }
    if (changed) { SaveClient(); WriteVoiceLive(); }
    return changed;
}

// Which of the game's keyboard controls (Options > Controller Setup > Keyboard, kept in the game's settings.ini)
// already uses vk; nullptr when none does.
const char* App::Impl::GameKeyClash(int vk)
{
    struct C { const char* ini; const char* name; int def; };
    static const C controls[] = { { "Up", "Up", 0x26 }, { "Down", "Down", 0x28 }, { "Left", "Left", 0x25 }, { "Right", "Right", 0x27 },
                                  { "A", "A", 'A' }, { "B", "B", 'S' }, { "X", "X", 'Z' }, { "Y", "Y", 'X' },
                                  { "LB", "LB", 'Q' }, { "RB", "RB", 'W' }, { "LT", "LT", 'E' }, { "RT", "RT", 'R' },
                                  { "LS", "LS", 'D' }, { "RS", "RS", 'F' }, { "Start", "Start", 0x0D }, { "Back", "Back", 0x08 } };
    for (const C& c : controls) {
        const std::string v = game.Get("Keyboard", c.ini, "");
        int key = c.def;
        if (!v.empty()) {
            char* end = nullptr;
            const long n = std::strtol(v.c_str(), &end, 0);
            if (end != v.c_str()) key = int(n);
        }
        if (key && key == vk) return c.name;
    }
    return nullptr;
}

// A key chosen by pressing it (voice: push to talk, or the open mic's mute). def 0: no key until one is chosen ("Clear"
// takes it away again); otherwise "Reset" goes back to def.
void App::Impl::KeyBindRow(const char* target, const char* title, const char* hint, int def)
{
    const float ctl = 380;   // the Settings page's controls
    const int vk = client.GetInt("voice", target, def);
    RowLabel(title, hint, ctl);
    const float sideW = 84;
    const bool listening = pttListen && pttTarget == target;
    ImGui::PushID(target);
    if (listening) {
        ImGui::PushStyleColor(ImGuiCol_Button, V(col::goldDeep));
        Button("Press a key...##bind", { ctl - sideW - 8, 34 }, false);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 8);
        if (Button("Cancel", { sideW, 34 })) StopPttListen();
    } else {
        const std::string label = (vk ? VkName(vk) : std::string("Not set")) + "##bind";
        if (Button(label.c_str(), { ctl - sideW - 8, 34 }, pttCooldown == 0 && !pttListen)) StartPttListen(target);
        ImGui::SameLine(0, 8);
        if (Button(def ? "Reset" : "Clear", { sideW, 34 }, vk != def)) {
            client.Set("voice", target, std::to_string(def));
            SaveClient();
            WriteVoiceLive();
        }
    }
    ImGui::PopID();
    if (!listening && vk) {
        if (const char* clash = GameKeyClash(vk)) {
            char t[240];
            std::snprintf(t, sizeof t, "%s is also the game's keyboard %s button, so pressing it does both. "
                                       "Pick another key, or change the game's key in Options > Controller Setup > Keyboard.",
                          VkName(vk).c_str(), clash);
            ImGui::Dummy({ 0, 2 });
            ImGui::PushFont(F.body, 13.f);
            Wrapped(col::amber, t);
            ImGui::PopFont();
        }
    }
}

// Keys that can be the push-to-talk key: not left / right click (they press the client's buttons), not Esc (it
// cancels), not the Windows / menu keys or Print Screen (Windows takes them), and the left / right variants of Shift,
// Ctrl and Alt rather than the shared codes.
static bool PttBindable(int vk)
{
    switch (vk) {
    case 0x01: case 0x02: case 0x03: case 0x10: case 0x11: case 0x12: case 0x1B: case 0x2C: case 0x5B: case 0x5C:
    case 0x5D: case 0x5F: case 0xE5: case 0xE7: return false;
    default: break;
    }
    if ((vk >= 0x15 && vk <= 0x1A) || (vk >= 0x1C && vk <= 0x1F)) return false;   // IME
    if (vk >= 0xA6 && vk <= 0xB7) return false;                                    // browser, media and app keys
    return vk > 0 && vk < 0xFF;
}

void App::Impl::StartPttListen(const char* target)
{
    pttTarget = target;
    for (int vk = 1; vk < 256; ++vk) pttHeld[vk] = plat::KeyHeld(vk);   // only a fresh press counts
    pttListen = true;
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;  // Space / Enter must not press the button
}

void App::Impl::StopPttListen()
{
    pttListen = false;
    pttCooldown = 3;   // a few frames before the keyboard drives the client again: the key just pressed stays out of it
}

void App::Impl::PollPttListen()
{
    if (!pttListen) {
        if (pttCooldown > 0 && --pttCooldown == 0) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        return;
    }
    const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    for (int vk = 1; vk < 256; ++vk) {
        const bool now = plat::KeyHeld(vk), was = pttHeld[vk];
        pttHeld[vk] = now;
        if (!focused || !now || was) continue;   // a key pressed in another window doesn't count
        if (vk == 0x1B) { StopPttListen(); return; }
        if (!PttBindable(vk)) continue;
        client.Set("voice", pttTarget, std::to_string(vk));
        SaveClient();
        WriteVoiceLive();
        StopPttListen();
        return;
    }
}

// Your team for online matches (the same one practice uses): duo, solo or pair play, and who.
void App::Impl::TeamCard()
{
    BeginCard("team", 0, "Your team");
    const float ctl = 420;
    static const char* teams[] = { "Duo (tag)", "Solo", "Pair play" };
    const std::string team = client.Get("picks", "team", "duo");
    int t = team == "solo" ? 1 : team == "pair" ? 2 : 0;
    RowLabel("Team", t == 2 ? "Online PAIR PLAY, through the game's own room. Your opponent has to choose Pair play too."
                   : "You go straight into the fight with this team.", ctl);
    if (Segmented("team", &t, teams, 3, ctl)) { client.Set("picks", "team", t == 1 ? "solo" : t == 2 ? "pair" : "duo"); SaveClient(); }
    Divider();
    if (client.GetInt("picks", "c1", -1) < 0) client.Set("picks", "c1", "14");
    if (client.GetInt("picks", "c2", -1) < 0) client.Set("picks", "c2", "42");
    if (t == 2) {
        ImGui::PushFont(F.body, 14.f);
        Wrapped(col::dim, "Pair play matches open in the game's own online room, as before: the host goes to ONLINE MODE, PLAYER MATCH, "
                          "CREATE SESSION, the other PC is taken in by itself, and you set up PAIR PLAY and pick your characters in the game.");
        ImGui::PopFont();
    } else {
        RowLabel(t ? "Character" : "Characters", "Random picks one for you when the match starts.", ctl);
        CharacterCombo("c1", "c1", true);
        if (!t) { ImGui::SameLine(0, 10); CharacterCombo("c2", "c2", true); }
        // the game's own rank of each pick (its online rank, from your save): ranked matches move it
        if (SDL_GetTicks() / 1000.0 - ranksReadAt > 3.0) ReadRanks();
        const int c1 = client.GetInt("picks", "c1", 14), c2 = client.GetInt("picks", "c2", 42);
        auto rank = [&](int c) { return std::string(c == roster::kRandom || c < 0 || c >= 64 || gameRank[c] < 0 ? "-" : roster::RankName(gameRank[c])); };
        ImGui::Dummy({ 0, 2 });
        RowLabel("Rank", ranksKnown ? (serverRanks ? "The game's own rank of each character, kept by the server: ranked matches move it by the game's own rules."
                                                 : "The game's own rank of each character, from your save. Ranked matches move it by the game's rules.")
                                    : "The game's own rank of each character shows here once the game has been started from the client.", ctl);
        const float x0 = ImGui::GetCursorPosX();
        Text(F.bold, 15.f, ranksKnown ? col::gold : col::faint, ranksKnown ? rank(c1).c_str() : "-");
        if (!t) { ImGui::SameLine(x0 + 215); Text(F.bold, 15.f, ranksKnown ? col::gold : col::faint, ranksKnown ? rank(c2).c_str() : "-"); }
    }
    EndCard();
}

// Practice: your team against a training partner of your choice, on the stage you choose.
void App::Impl::PracticeCard()
{
    BeginCard("practice", 0, "Practice");
    const float ctl = 420;
    static const char* teams[] = { "Duo (tag)", "Solo" };
    // your side: the same team as online matches (ONLINE > Your team)
    int me = client.Get("picks", "team", "duo") == "solo" ? 1 : 0;
    if (client.GetInt("picks", "c1", -1) < 0) client.Set("picks", "c1", "14");
    if (client.GetInt("picks", "c2", -1) < 0) client.Set("picks", "c2", "42");
    RowLabel("You", "Your side in practice.", ctl);
    if (Segmented("pteam", &me, teams, 2, ctl)) { client.Set("picks", "team", me ? "solo" : "duo"); SaveClient(); }
    ImGui::Dummy({ 0, 4 });
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ctl);
    CharacterCombo("pc1", "c1", true);
    if (!me) { ImGui::SameLine(0, 10); CharacterCombo("pc2", "c2", true); }
    Divider();
    if (client.GetInt("practice", "o1", -1) < 0) client.Set("practice", "o1", "13");
    if (client.GetInt("practice", "o2", -1) < 0) client.Set("practice", "o2", "9");
    int t = client.Get("practice", "oteam", "duo") == "solo" ? 1 : 0;
    RowLabel("Opponent", "Your training partner.", ctl);
    if (Segmented("oteam", &t, teams, 2, ctl)) { client.Set("practice", "oteam", t ? "solo" : "duo"); SaveClient(); }
    ImGui::Dummy({ 0, 4 });
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ctl);
    CharacterCombo("o1", "o1", true);
    if (!t) { ImGui::SameLine(0, 10); CharacterCombo("o2", "o2", true); }
    Divider();
    RowLabel("Stage", "Random for now: picking the stage comes next.", ctl);
    const int stage = client.GetInt("practice", "stage", -1);
    ImGui::SetNextItemWidth(ctl);
    if (ImGui::BeginCombo("##stage", stage < 0 ? "Random" : roster::StageName(stage), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("Random", stage < 0)) { client.Set("practice", "stage", "-1"); SaveClient(); }
        for (const roster::Stage& st : roster::kStages)
            if (ImGui::Selectable(st.name, st.id == stage)) { client.Set("practice", "stage", std::to_string(st.id)); SaveClient(); }
        ImGui::EndCombo();
    }
    ImGui::Dummy({ 0, 10 });
    const bool busy = practiceProc || matchProc;
    if (BigButton(practiceProc ? "PRACTICE RUNNING" : "START PRACTICE", { 280, 48 }, !busy && installed)) StartPractice();
    if (!practiceNote.empty()) { ImGui::SameLine(0, 12); Text(F.body, 13.f, col::amber, practiceNote.c_str()); }
    EndCard();
}

// A found match waiting for this player's answer: a sound and a taskbar flash once, the question in the game when they
// are practising (match_prompt.h in the port), and their answer from there. The client's own pad and keyboard navigation
// pause while the question is up: A / B / Enter / Esc answer it (the ONLINE page's card) instead.
void App::Impl::TickAccept()
{
    if (!online) return;
    const Online::View v = online->Snapshot();
    const bool asking = v.state == Online::CHECKING && !v.accepted;
    if (asking && promptFor != v.match.id) {
        promptFor = v.match.id;
        plat::PlayAlert();
        if (!(SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS)) SDL_FlashWindow(window, SDL_FLASH_UNTIL_FOCUSED);
        page = ONLINE;
        if (practiceProc) { WritePrompt(&v); promptInGame = true; }
    }
    if (asking && promptInGame) {
        // the game's answer: id=<match>, answer=accept|decline, end=1
        std::string t;
        const std::string answer = plat::JoinPath(plat::ClientDataDir(), "match_prompt.ini.answer");
        if (plat::ReadText(answer, t) && t.find("end=1") != std::string::npos && t.find("id=" + v.match.id + "\n") != std::string::npos) {
            if (t.find("answer=accept") != std::string::npos) online->Accept();
            else online->Decline();
            plat::WriteText(answer, "");
        }
    }
    if (!asking && promptInGame) { WritePrompt(nullptr); promptInGame = false; }
    if (asking && !practiceProc) promptInGame = false;   // practice closed: the client's card is the only question
    ImGuiIO& io = ImGui::GetIO();
    if (asking && !acceptNavOff) { io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard); acceptNavOff = true; }
    if (!asking && acceptNavOff) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        if (!pttListen && pttCooldown == 0) io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        acceptNavOff = false;
    }
}

// The question for the game (TTT2_PROMPT): nullptr takes it down.
void App::Impl::WritePrompt(const Online::View* v)
{
    const std::string file = plat::JoinPath(plat::ClientDataDir(), "match_prompt.ini");
    if (!v) { plat::WriteText(file, "show=0\nend=1\n"); return; }
    const int64_t leftMs = std::max<int64_t>(0, int64_t(v->acceptDeadlineMs) - int64_t(net::NowMs()));
    const int64_t unixNow = int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
    const std::string line = "vs " + v->match.opponent + " (" + std::to_string(v->match.opponentRating) + ")  ·  " +
                             (v->match.mode == "ranked" ? "Ranked" : "Casual");
    plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "match_prompt.ini.answer"), "");
    plat::WriteText(file, "id=" + v->match.id + "\nline=" + line + "\ndeadline=" + std::to_string(unixNow + leftMs) + "\nshow=1\nend=1\n");
}

// While searching (ONLINE): practice with what's set on PLAY, in one click.
void App::Impl::PracticeQuickCard()
{
    BeginCard("practicequick", 0, "Practice while you wait");
    auto name = [](int id) { return id < 0 || id == roster::kRandom ? std::string("Random") : std::string(roster::CharacterName(id)); };
    const bool solo = client.Get("picks", "team", "duo") == "solo", osolo = client.Get("practice", "oteam", "duo") == "solo";
    std::string me = name(client.GetInt("picks", "c1", 14)), them = name(client.GetInt("practice", "o1", 13));
    if (!solo) me += " & " + name(client.GetInt("picks", "c2", 42));
    if (!osolo) them += " & " + name(client.GetInt("practice", "o2", 9));
    const int stage = client.GetInt("practice", "stage", -1);
    const std::string line = me + "  vs  " + them + "   ·   " + (stage < 0 ? std::string("Random stage") : std::string(roster::StageName(stage)));
    Text(F.bold, 15.f, col::text, line.c_str());
    ImGui::Dummy({ 0, 8 });
    const bool busy = practiceProc || matchProc;
    if (BigButton(practiceProc ? "PRACTICE RUNNING" : "START PRACTICE", { 260, 46 }, !busy && installed)) StartPractice();
    ImGui::SameLine(0, 12);
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 7);
    if (Button("Change on PLAY", { 150, 32 })) page = PLAY;
    if (!practiceNote.empty()) { ImGui::Dummy({ 0, 4 }); Text(F.body, 13.f, col::amber, practiceNote.c_str()); }
    ImGui::PushFont(F.body, 13.f);
    Wrapped(col::faint, "When a match is found, practice closes and the match starts.");
    ImGui::PopFont();
    EndCard();
}


// Replays: the game saves every online fight (TTT2_REPLAY_DIR); the newest 50 stay, and any starred one (moved to
// replays\favourites) stays for good. Watch plays one in the game (TTT2_REPLAY).
namespace {
struct ReplayInfo { std::string path, date, p1, p2; bool fav = false; };
ReplayInfo ReadReplayInfo(const std::string& path, bool fav)
{
    ReplayInfo r;
    r.path = path;
    r.fav = fav;
    std::string t;
    if (plat::ReadBinary(path, t)) {
        const size_t end = std::min(t.find("\ndata\n"), size_t(4096));
        size_t at = 0;
        while (at < end) {
            size_t nl = t.find('\n', at);
            if (nl == std::string::npos || nl > end) nl = end;
            const std::string line = t.substr(at, nl - at);
            if (line.rfind("p1=", 0) == 0) r.p1 = line.substr(3);
            else if (line.rfind("p2=", 0) == 0) r.p2 = line.substr(3);
            else if (line.rfind("date=", 0) == 0) r.date = line.substr(5);
            at = nl + 1;
        }
    }
    if (r.date.empty()) {
        const size_t slash = path.find_last_of("/\\");
        r.date = path.substr(slash == std::string::npos ? 0 : slash + 1);
    }
    return r;
}
}  // namespace

void App::Impl::StartReplay(const std::string& file)
{
    replayNote.clear();
    if (practiceProc && !plat::ProcessDone(practiceProc)) { replayNote = "Close the game first."; return; }
    if (!EnsureOnlineExe(replayNote)) return;
    auto env = CommonEnv();
    env.insert(env.end(), { { "TTT2_BOOT", "versus" }, { "TTT2_REPLAY", file } });
    std::string err;
    practiceProc = plat::LaunchWithEnv(installDir, plat::kGameExe, env, err);
    if (practiceProc) gameRunning = true;
    else replayNote = "Could not start the game: " + err;
}

void App::Impl::ReplaysCard()
{
    BeginCard("replays", 0, "Replays");
    const std::string dir = ReplaysDir(), favDir = plat::JoinPath(dir, "favourites");
    static double listedAt = -100;
    static std::vector<ReplayInfo> list;
    const double now = SDL_GetTicks() / 1000.0;
    if (now - listedAt > 3) {   // the list, every few seconds (new fights land while the client runs)
        listedAt = now;
        list.clear();
        std::vector<std::string> recent;
        for (const std::string& f : plat::ListFilesRecursive(dir)) {
            if (f.size() < 4 || f.substr(f.size() - 4) != ".ttr") continue;
            const bool fav = f.find("favourites") != std::string::npos;
            if (!fav) recent.push_back(f);
            list.push_back(ReadReplayInfo(f, fav));
        }
        // the newest 50 (not starred) stay
        std::sort(recent.begin(), recent.end());
        if (recent.size() > 50)
            for (size_t i = 0; i + 50 < recent.size(); ++i) plat::DeleteFileAt(recent[i]);
        std::sort(list.begin(), list.end(), [](const ReplayInfo& a, const ReplayInfo& b) {
            if (a.fav != b.fav) return a.fav;
            return a.date > b.date;
        });
        if (list.size() > 60) list.resize(60);
    }
    if (list.empty()) {
        Wrapped(col::dim, "Your online fights show up here after you play them. The newest 50 are kept; star one to keep it for good.");
    }
    int shown = 0;
    for (size_t i = 0; i < list.size() && shown < 12; ++i, ++shown) {
        const ReplayInfo& r = list[i];
        ImGui::PushID(int(i));
        const std::string who = (r.p1.empty() ? std::string("Player 1") : r.p1) + "  vs  " + (r.p2.empty() ? std::string("Player 2") : r.p2);
        Text(F.bold, 14.f, r.fav ? col::gold : col::text, (std::string(r.fav ? "* " : "") + who).c_str());
        ImGui::SameLine(0, 12);
        Text(F.body, 13.f, col::dim, r.date.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 290);
        if (Button("Watch", { 90, 26 }, installed)) StartReplay(r.path);
        ImGui::SameLine(0, 8);
        if (Button(r.fav ? "Unstar" : "Star", { 90, 26 })) {
            const size_t slash = r.path.find_last_of("/\\");
            const std::string name = r.path.substr(slash + 1);
            plat::MakeDir(favDir);
            plat::MoveFileReplace(r.path, plat::JoinPath(r.fav ? dir : favDir, name));
            listedAt = -100;
        }
        ImGui::SameLine(0, 8);
        if (Button("Delete", { 90, 26 })) { plat::DeleteFileAt(r.path); listedAt = -100; }
        ImGui::PopID();
    }
    if (list.size() > size_t(shown)) Wrapped(col::dim, ("and " + std::to_string(list.size() - size_t(shown)) + " more in the replays folder").c_str());
    ImGui::Dummy({ 0, 4 });
    if (Button("Open folder", { 130, 28 })) { plat::MakeDir(dir); plat::OpenFolder(dir); }
    if (!replayNote.empty()) { ImGui::SameLine(0, 12); Text(F.body, 13.f, col::amber, replayNote.c_str()); }
    EndCard();
}

void App::Impl::StartPractice()
{
    practiceNote.clear();
    if (!EnsureOnlineExe(practiceNote)) return;
    // practice places the characters itself: a random pick is made here
    auto pick = [](int id) {
        if (id != roster::kRandom && id >= 0) return id;
        const size_t n = sizeof roster::kCharacters / sizeof roster::kCharacters[0];
        return roster::kCharacters[size_t(SDL_rand(int(n)))].id;   // (a DLC one the game lacks: its Random)
    };
    const bool solo = client.Get("picks", "team", "duo") == "solo", osolo = client.Get("practice", "oteam", "duo") == "solo";
    const int a = pick(client.GetInt("picks", "c1", 14)), b = solo ? a : pick(client.GetInt("picks", "c2", 42));
    const int c = pick(client.GetInt("practice", "o1", 13)), d = osolo ? c : pick(client.GetInt("practice", "o2", 9));
    auto env = CommonEnv();
    env.insert(env.end(), { { "TTT2_BOOT", "practice" },   // the game walks its character select to these picks
                            { "TTT2_FIGHTERS", std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c) + "," + std::to_string(d) },
                            { "TTT2_STAGE", std::to_string(client.GetInt("practice", "stage", -1)) } });
    std::string err;
    practiceProc = plat::LaunchWithEnv(installDir, plat::kGameExe, env, err);
    if (!practiceProc) practiceNote = "Could not start the game: " + err;
}

// The test logs are one file now (the user, 2026-11-25: one file, not new ones every match): the separate files of
// earlier test builds (their names start with the match's time) go.
void App::Impl::PruneTestLogs()
{
    for (const std::string& f : plat::ListFilesRecursive(TestLogsDir())) {
        const size_t slash = f.find_last_of("/\\");
        const std::string name = slash == std::string::npos ? f : f.substr(slash + 1);
        if (!name.empty() && name[0] >= '0' && name[0] <= '9') plat::DeleteFileAt(plat::JoinPath(TestLogsDir(), name));
    }
}

// The game of a match has closed: its logs go into the test logs, and its result to the server.
void App::Impl::MatchGameClosed()
{
    online->ReclaimUdp();
    {
        // the match's part of test log.txt: the games it finished, the game's trace, its full log - the last 5 matches
        std::string games, trace, log;
        plat::ReadBinary(ResultsFile(), games);
        const std::string traceFile = plat::JoinPath(plat::GameDataDir(), "trace.txt");
        plat::ReadBinary(traceFile, trace);
        plat::ReadBinary(plat::JoinPath(plat::GameDataDir(), "log.txt"), log);
        // Kept small on disk (the user, 2026-11-25): a long trace or log keeps its start and its end (where a match's
        // trouble shows), and the whole file stays under 4 MB - the oldest matches go first.
        auto trim = [](std::string& text, size_t cap) {
            if (text.size() <= cap) return;
            const size_t head = cap / 4, tail = cap - head;
            text = text.substr(0, head) + "\r\n.... (" + std::to_string((text.size() - cap) / 1024) + " KB cut here) ....\r\n" +
                   text.substr(text.size() - tail);
        };
        trim(trace, 400 * 1024);
        trim(log, 600 * 1024);
        static const std::string kHead = "\r\n==================== MATCH ";
        std::string part = kHead + logStamp + " ====================\r\n" + "---- the games it finished ----\r\n" +
                           (games.empty() ? std::string("(none)\r\n") : games) + "\r\n---- the game's trace ----\r\n" + trace +
                           "\r\n---- the game's log ----\r\n" + log;
        const std::string all = plat::JoinPath(TestLogsDir(), "test log.txt");
        std::string before;
        plat::ReadBinary(all, before);
        std::vector<size_t> starts;
        for (size_t at = before.find(kHead); at != std::string::npos; at = before.find(kHead, at + 1)) starts.push_back(at);
        if (starts.size() >= 5) before.erase(0, starts[starts.size() - 4]);   // 4 earlier matches stay, this one makes 5
        while (before.size() + part.size() > 4u * 1024 * 1024) {
            const size_t next = before.find(kHead, 1);
            if (next == std::string::npos) { before.clear(); break; }
            before.erase(0, next);
        }
        plat::WriteText(all, before + part);
        plat::DeleteFileAt(traceFile);
    }
    // who won: the game wrote every finished game down; the set's winner goes to the server, nobody is asked
    setScore = ReadScore();
    char t[160];
    if (setScore.won != setScore.lost) {
        const bool won = setScore.won > setScore.lost;
        online->Report(won, FightsJson());
        resultSent = true;
        std::snprintf(t, sizeof t, "Last match: you %s %d - %d against %s", won ? "won" : "lost", setScore.won, setScore.lost,
                      resultFor.opponent.c_str());
        lastSet = t;
        lastSetFor = resultFor.id;
        lastRanks = ReadRankChanges();
    } else
        noResult = true;
    ReadRanks();
    Probe();
}

void App::Impl::TickMatch()
{
    if (!online) return;
    TakeServerRanks();
    if (practiceProc && plat::ProcessDone(practiceProc)) practiceProc = 0;
    if (matchProc && plat::ProcessDone(matchProc)) {
        matchProc = 0;
        MatchGameClosed();
    } else if (matchProc && SDL_GetTicks() / 1000.0 - scoreReadAt > 1.0) {
        scoreReadAt = SDL_GetTicks() / 1000.0;
        setScore = ReadScore();
    }
    const Online::View v = online->Snapshot();
    if (v.state == Online::READY && !matchProc && !resultSent && !noResult && matchStarted != v.match.id && installed) StartMatch(v.match);
    if (v.state != Online::READY) resultSent = noResult = false;   // the server settled it (both reported, or a forfeit), or left
}

// The games the game has written for this match so far (TTT2_RESULT, match_result.h in the game: one line per game).
App::Impl::SetScore App::Impl::ReadScore()
{
    SetScore s;
    std::string text;
    if (!plat::ReadBinary(ResultsFile(), text)) return s;
    size_t at = 0;
    while (at < text.size()) {
        size_t end = text.find('\n', at);
        if (end == std::string::npos) break;   // a line still being written
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.find("result=won") != std::string::npos) ++s.won;
        else if (line.find("result=lost") != std::string::npos) ++s.lost;
        else if (line.find("result=draw") != std::string::npos) ++s.draws;
    }
    return s;
}

void App::Impl::UpdateBanner()
{
    TickMatch();
    TickAccept();
    // after an update: the game's controller layer follows the client's (once; may ask for admin)
    if (justUpdated && !builder.Running()) {
        justUpdated = false;
        CheckLayer();
        if (layerOutdated && !gameRunning) UpdateLayer();
    }
    if (builder.Finished() && page != INSTALL) {   // a layer update started from here
        builder.Acknowledge();
        CheckLayer();
        Probe();
    }
    if (autoInstall && updater.GetState() == Updater::READY) { autoInstall = false; InstallUpdate(); }
    const Updater::State st = updater.GetState();
    if (st != Updater::AVAILABLE && st != Updater::DOWNLOADING && st != Updater::READY && updateNote.empty() &&
        !(st == Updater::FAILED && !updater.Latest().empty()))
        return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(Alpha(col::gold, 0.10f)));
    ImGui::PushStyleColor(ImGuiCol_Border, V(Alpha(col::gold, 0.45f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 20, 14 });
    ImGui::BeginChild("update", { 0, 0 }, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    std::string title, sub;
    if (!updateNote.empty()) { title = "Update"; sub = updateNote; }
    else if (st == Updater::AVAILABLE) { title = "TTT2 Client " + updater.Latest() + " is out"; sub = std::string("You have ") + TTT2_CLIENT_VERSION + ". One click: it downloads, installs and restarts."; }
    else if (st == Updater::DOWNLOADING) { title = "Updating to " + updater.Latest(); sub = "Downloading and checking it. The client restarts by itself."; }
    else if (st == Updater::READY) { title = "Installing " + updater.Latest(); sub = "Restarting..."; }
    else { title = "Update failed"; sub = updater.Error(); }
    ImGui::BeginGroup();
    Text(F.display, 20.f, col::text, title.c_str());
    ImGui::PushFont(F.body, 13.f);
    Wrapped(st == Updater::FAILED ? col::red : col::dim, sub.c_str());
    ImGui::PopFont();
    if (st == Updater::DOWNLOADING) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, V(col::gold));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, V(col::cardHi));
        ImGui::ProgressBar(updater.Progress(), { 300, 6 }, "");
        ImGui::PopStyleColor(2);
    }
    ImGui::EndGroup();
    if (updateNote.empty()) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 160 + ImGui::GetStyle().WindowPadding.x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
        if (st == Updater::AVAILABLE || st == Updater::FAILED) {
            if (BigButton(st == Updater::FAILED ? "TRY AGAIN" : "UPDATE", { 160, 40 })) { autoInstall = true; updater.Download(); }
        }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    ImGui::Dummy({ 0, 12 });
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

    // (no LB / RB page change: the user, 2026-11-23.) Holding X / Square opened Dear ImGui's window switcher (a "root"
    // list): it is off for the pad.
    ImGui::GetCurrentContext()->ConfigNavWindowingWithGamepad = false;

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
    UpdateBanner();
    if (pttListen && page != SETTINGS) StopPttListen();
    PollPttListen();
    if (liveDirty && now - liveWrittenAt > 0.1) WriteVoiceLive();   // a volume being dragged: the game follows live
    switch (page) {
    case PLAY: Header("PLAY", "Your game, your controller, ready to fight."); PagePlay(); break;
    case ONLINE: PageOnline(); break;
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
void App::JustUpdated() { p_->justUpdated = true; }
bool App::WantsQuit() const { return p_->wantsQuit; }
void App::SetPage(int page) { p_->page = Impl::Page(std::clamp(page, 0, int(Impl::PAGE_COUNT) - 1)); }
