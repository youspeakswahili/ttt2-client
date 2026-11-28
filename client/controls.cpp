// TTT2 Client - controllers: the ones plugged in, the player's button map for each (common/input_config.h, read by the
// game's controller layer, xinput1_4.dll in the game build), the input test and the CONTROLS page.
#include "app_impl.h"

#include <cctype>
#include <cmath>
#include <cstdio>

using namespace ui;
using namespace ttt2;

void App::Impl::SaveInput() { plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "input.ini"), WriteConfig(input)); }

void App::Impl::SyncDeviceHold()
{
    const bool suspend = gameRunning && page != CONTROLS;
    if (suspend == devicesSuspended) return;
    devicesSuspended = suspend;
    if (suspend) {
        if (captureOut >= 0) StopCapture();
        for (auto& d : devices) { if (d.pad) SDL_CloseGamepad(d.pad); else SDL_CloseJoystick(d.joy); }
        devices.clear();
    } else {
        int n = 0;
        if (SDL_JoystickID* ids = SDL_GetJoysticks(&n)) { for (int i = 0; i < n; ++i) AddDevice(ids[i]); SDL_free(ids); }
    }
}
void App::Impl::AddDevice(SDL_JoystickID id)
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
void App::Impl::RemoveDevice(SDL_JoystickID id)
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
Device* App::Impl::Selected()
{
    for (auto& d : devices) if (d.guid == selectedGuid) return &d;
    return nullptr;
}

// -------------------------------------------------- capture
void App::Impl::StartCapture(int out, bool append)
{
    Device* d = Selected();
    if (!d) return;
    captureOut = out;
    captureAppend = append;
    captureWaitRelease = true;
    captureStart = SDL_GetTicks() / 1000.0;
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableGamepad;   // the pad is being listened to
}
void App::Impl::StopCapture()
{
    captureOut = -1;
    wizard = false;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
}
void App::Impl::UpdateCapture()
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
    while (wizard && ++wizardIndex < int(kRowCount) && !kRows[wizardIndex].wizard) {}
    if (wizard && wizardIndex < int(kRowCount)) {   // the wizard walks the rows marked for it
        captureOut = kRows[wizardIndex].out;
        captureAppend = false;
        captureWaitRelease = true;
        captureStart = now;
        return;
    }
    StopCapture();
}
// A gamepad's SDL layout written out as raw sources, so one changed button keeps the others
void App::Impl::SeedFromGamepad(const Device& d, DeviceMap& m)
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
bool App::Impl::OutHeld(const Device& d, int out)
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
    const ImU32 off = IM_COL32(40, 40, 50, 255), body = IM_COL32(24, 24, 31, 255), dark = col::ink;
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
        d->AddCircleFilled({ c.x + dx * sr * 0.45f, c.y + dy * sr * 0.45f }, sr * 0.62f, click ? col::accent : moved ? col::red : IM_COL32(58, 58, 70, 255), 32);
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
            d->AddCircleFilled(bc, br, on ? col::accent : IM_COL32(16, 16, 22, 255), 28);
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
        d->AddRectFilled({ c.x - 9, c.y - 5 }, { c.x + 9, c.y + 5 }, held(out) ? col::accent : off, 5.f);
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
        d->AddCircleFilled(bc, s * 0.42f, on ? (face ? col::accent : col::red) : IM_COL32(32, 32, 40, 255), 40);
        d->AddCircle(bc, s * 0.42f, on ? col::text : col::line, 40, 1.5f);
        ImGui::PushFont(face ? F.heavy : F.bold, face ? s * 0.42f : s * 0.24f);
        const ImVec2 ts = ImGui::CalcTextSize(b.label);
        d->AddText({ bc.x - ts.x * 0.5f, bc.y - ts.y * 0.5f }, on ? col::ink : col::dim, b.label);
        ImGui::PopFont();
    }
    // back / start, bottom right
    for (int i = 0; i < 2; ++i) {
        const int out = i ? O_START : O_BACK;
        const ImVec2 sc = { p.x + w - 70 + float(i) * 40, p.y + height - 34 };
        d->AddRectFilled({ sc.x - 14, sc.y - 6 }, { sc.x + 14, sc.y + 6 }, held(out) ? col::accent : IM_COL32(32, 32, 40, 255), 6.f);
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
    PageHeader("CONTROLS", "Every controller works: pads, fight sticks and hitboxes. Map any button here; the game picks it up at once.");
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
        d->AddRectFilled(p, { p.x + w, p.y + 54 }, sel ? Alpha(col::accent, 0.12f) : hot ? col::cardHi : 0, 8.f);
        if (sel) d->AddRect(p, { p.x + w, p.y + 54 }, Alpha(col::accent, 0.6f), 8.f);
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
            d->AddText({ p.x + 12, p.y + 30 }, custom ? col::accent : col::dim, custom ? "Custom map" : kind);
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
        for (int ri = 0; ri < kRowCount; ++ri) {
            const OutInfo& row = kRows[ri];
            ImGui::PushID(row.out);
            ImGui::TableNextRow(0, 40);
            const bool on = dv && OutHeld(*dv, row.out);
            const bool capturing = captureOut == row.out;
            if (on || capturing) ImGui::TableSetBgColor(ImGuiTableBgTarget_RowBg1, capturing ? Alpha(col::red, 0.16f) : Alpha(col::accent, 0.14f));
            ImGui::TableNextColumn();
            ImGui::AlignTextToFramePadding();
            Text(F.heavy, 22.f, on ? col::accent : col::text, row.name);
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
