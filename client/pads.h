// TTT2 Client - controllers as the client shows them: each device, its layout family and button names.
#pragma once
#include "../common/input_config.h"
#include <SDL3/SDL.h>
#include <string>

struct Device {
    SDL_JoystickID id = 0;
    SDL_Joystick* joy = nullptr;
    SDL_Gamepad* pad = nullptr;
    std::string guid, name;
    ttt2::DeviceMap generic;   // the layout the game uses for it until it is mapped (no SDL gamepad)
};

// What each output is called on the Controls page (Tekken meaning of the game's default layout)
struct OutInfo { int out; const char* name; const char* role; bool wizard; };
extern const OutInfo kRows[];
extern const int kRowCount;

enum PadFamily { FAM_XBOX, FAM_PLAYSTATION, FAM_NINTENDO, FAM_OTHER };
std::string SourceName(const ttt2::Source& s);
PadFamily FamilyOf(SDL_Gamepad* pad);
PadFamily FamilyOfVendor(uint16_t vendor);
PadFamily DeviceFamily(const Device& d);
const char* PadButtonName(PadFamily f, int out);
const char* DefaultBinding(int out);
bool SourceHeld(SDL_Joystick* j, const ttt2::Source& s, int deadzone);
