// TTT2 Client - controllers as the client shows them.
#include "pads.h"
#include <algorithm>
#include <cstdio>

using namespace ttt2;


// What each output is called on the Controls page (Tekken meaning of the game's default layout)
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
const int kRowCount = int(sizeof kRows / sizeof kRows[0]);

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