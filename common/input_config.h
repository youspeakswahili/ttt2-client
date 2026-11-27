// TTT2 Client - controller mapping shared by the client (which writes it) and the controller layer
// (xinput9_1_0.dll, which reads it). Plain text, %LOCALAPPDATA%\TTT2Client\input.ini:
//
//   [general]
//   socd_lr = neutral | last | off        left + right held together
//   socd_ud = up | neutral | last | off   up + down held together
//   stick   = analog | dpad | off         a gamepad's left stick (devices with a custom map ignore it)
//   deadzone = 0..90                     percent, stick and axis-as-button threshold
//
//   [device <SDL GUID>]                  one custom map per controller model; absent = SDL's gamepad layout
//   name = Victrix Pro FS
//   A = b1                               output = sources, comma separated (one source may feed several
//   X = b0,b6                            outputs: that makes a macro, e.g. 1+2 on one button)
//   UP = h0u                             b<n> button, h<n><u|d|l|r> hat direction, a<n>+ / a<n>- axis half
//   LT = a4+
//
// Outputs are the Xbox 360 controller the game reads. What each one does in a fight is the game's
// own setting (OPTIONS -> CONTROLLER SETUP); out of the box X = 1 (LP), Y = 2 (RP), A = 3 (LK), B = 4 (RK), LB = Tag.
// M12 / M34 / M13 / M24 / M1234 / M134 / M23 / M14 are macros that press those face buttons together.
#pragma once
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace ttt2 {

enum Out : int { O_UP, O_DOWN, O_LEFT, O_RIGHT, O_START, O_BACK, O_LS, O_RS, O_LB, O_RB, O_A, O_B, O_X, O_Y, O_LT, O_RT,
                 // macros: one physical button presses several of the game's default buttons (X 1, Y 2, A 3, B 4)
                 O_M12, O_M34, O_M13, O_M24, O_M1234, O_M134, O_M23, O_M14, O_COUNT };

inline const char* OutKey(int o)
{
    static const char* k[O_COUNT] = { "UP", "DOWN", "LEFT", "RIGHT", "START", "BACK", "LS", "RS", "LB", "RB", "A", "B", "X", "Y", "LT", "RT",
                                      "M12", "M34", "M13", "M24", "M1234", "M134", "M23", "M14" };
    return o >= 0 && o < O_COUNT ? k[o] : "";
}
// XINPUT_GAMEPAD wButtons bit of each output (triggers: 0, they are analog bytes)
inline uint16_t OutBit(int o)
{
    // X 0x4000 (1), Y 0x8000 (2), A 0x1000 (3), B 0x2000 (4)
    static const uint16_t b[O_COUNT] = { 0x0001, 0x0002, 0x0004, 0x0008, 0x0010, 0x0020, 0x0040, 0x0080, 0x0100, 0x0200,
                                         0x1000, 0x2000, 0x4000, 0x8000, 0, 0,
                                         0x4000 | 0x8000, 0x1000 | 0x2000, 0x4000 | 0x1000, 0x8000 | 0x2000, 0xF000,
                                         0x4000 | 0x1000 | 0x2000, 0x8000 | 0x1000, 0x4000 | 0x2000 };
    return o >= 0 && o < O_COUNT ? b[o] : 0;
}

enum SrcKind : uint8_t { S_NONE, S_BUTTON, S_HAT, S_AXIS_POS, S_AXIS_NEG };
struct Source {
    SrcKind kind = S_NONE;
    uint8_t index = 0;
    uint8_t hatDir = 0;   // SDL_HAT_UP 1, RIGHT 2, DOWN 4, LEFT 8
    bool operator==(const Source& o) const { return kind == o.kind && index == o.index && hatDir == o.hatDir; }
};

inline std::string SourceText(const Source& s)
{
    char t[16] = {};
    switch (s.kind) {
    case S_BUTTON: std::snprintf(t, sizeof t, "b%u", s.index); break;
    case S_HAT: std::snprintf(t, sizeof t, "h%u%c", s.index, s.hatDir == 1 ? 'u' : s.hatDir == 2 ? 'r' : s.hatDir == 4 ? 'd' : 'l'); break;
    case S_AXIS_POS: std::snprintf(t, sizeof t, "a%u+", s.index); break;
    case S_AXIS_NEG: std::snprintf(t, sizeof t, "a%u-", s.index); break;
    default: break;
    }
    return t;
}
inline bool ParseSource(const char* p, Source& s)
{
    if (!p || !*p) return false;
    char* end = nullptr;
    const long n = std::strtol(p + 1, &end, 10);
    if (end == p + 1 || n < 0 || n > 255) return false;
    s = {};
    s.index = uint8_t(n);
    switch (p[0]) {
    case 'b': s.kind = S_BUTTON; return true;
    case 'h':
        s.kind = S_HAT;
        s.hatDir = *end == 'u' ? 1 : *end == 'r' ? 2 : *end == 'd' ? 4 : *end == 'l' ? 8 : 0;
        return s.hatDir != 0;
    case 'a':
        if (*end == '+') { s.kind = S_AXIS_POS; return true; }
        if (*end == '-') { s.kind = S_AXIS_NEG; return true; }
        return false;
    default: return false;
    }
}

enum SocdLR : int { LR_NEUTRAL, LR_LAST, LR_OFF };
enum SocdUD : int { UD_UP, UD_NEUTRAL, UD_LAST, UD_OFF };
enum StickMode : int { STICK_ANALOG, STICK_DPAD, STICK_OFF };

struct DeviceMap {
    std::string guid;   // SDL_GUIDToString
    std::string name;
    std::vector<Source> out[O_COUNT];
    bool empty() const
    {
        for (auto& v : out) if (!v.empty()) return false;
        return true;
    }
};

// The layout of a controller SDL does not know as a gamepad, until the player maps it: the first hat
// as the D-pad, buttons 1..4 as X A B Y (the usual DirectInput order: Square Cross Circle Triangle on a
// PlayStation-style stick), 5/6 LB RB, 7/8 LT RT, 9/10 BACK START.
inline DeviceMap GenericMap(int buttons, int hats)
{
    DeviceMap m;
    if (hats > 0) {
        m.out[O_UP].push_back({ S_HAT, 0, 1 });
        m.out[O_RIGHT].push_back({ S_HAT, 0, 2 });
        m.out[O_DOWN].push_back({ S_HAT, 0, 4 });
        m.out[O_LEFT].push_back({ S_HAT, 0, 8 });
    }
    static const int order[10] = { O_X, O_A, O_B, O_Y, O_LB, O_RB, O_LT, O_RT, O_BACK, O_START };
    for (int i = 0; i < 10 && i < buttons; ++i) m.out[order[i]].push_back({ S_BUTTON, uint8_t(i), 0 });
    return m;
}

struct InputConfig {
    int socdLR = LR_NEUTRAL;
    int socdUD = UD_UP;
    int stick = STICK_ANALOG;
    int deadzone = 30;
    std::vector<DeviceMap> devices;

    const DeviceMap* Find(const std::string& guid) const
    {
        for (auto& d : devices) if (d.guid == guid && !d.empty()) return &d;
        return nullptr;
    }
    DeviceMap& Get(const std::string& guid)
    {
        for (auto& d : devices) if (d.guid == guid) return d;
        devices.push_back({});
        devices.back().guid = guid;
        return devices.back();
    }
};

inline std::string Trim(std::string s)
{
    size_t a = 0, b = s.size();
    while (a < b && (s[a] == ' ' || s[a] == '\t' || s[a] == '\r' || s[a] == '\n')) ++a;
    while (b > a && (s[b - 1] == ' ' || s[b - 1] == '\t' || s[b - 1] == '\r' || s[b - 1] == '\n')) --b;
    return s.substr(a, b - a);
}

inline void ParseConfig(const std::string& text, InputConfig& cfg)
{
    cfg = {};
    DeviceMap* dev = nullptr;
    bool general = false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) nl = text.size();
        std::string line = Trim(text.substr(pos, nl - pos));
        pos = nl + 1;
        if (line.empty() || line[0] == ';' || line[0] == '#') continue;
        if (line[0] == '[') {
            const std::string sec = Trim(line.substr(1, line.find(']') == std::string::npos ? std::string::npos : line.find(']') - 1));
            general = sec == "general";
            dev = sec.rfind("device ", 0) == 0 ? &cfg.Get(Trim(sec.substr(7))) : nullptr;
            continue;
        }
        const size_t eq = line.find('=');
        if (eq == std::string::npos) continue;
        const std::string key = Trim(line.substr(0, eq)), val = Trim(line.substr(eq + 1));
        if (general) {
            if (key == "socd_lr") cfg.socdLR = val == "last" ? LR_LAST : val == "off" ? LR_OFF : LR_NEUTRAL;
            else if (key == "socd_ud") cfg.socdUD = val == "neutral" ? UD_NEUTRAL : val == "last" ? UD_LAST : val == "off" ? UD_OFF : UD_UP;
            else if (key == "stick") cfg.stick = val == "dpad" ? STICK_DPAD : val == "off" ? STICK_OFF : STICK_ANALOG;
            else if (key == "deadzone") { const int d = std::atoi(val.c_str()); cfg.deadzone = d < 0 ? 0 : d > 90 ? 90 : d; }
        } else if (dev) {
            if (key == "name") { dev->name = val; continue; }
            for (int o = 0; o < O_COUNT; ++o) {
                if (key != OutKey(o)) continue;
                size_t p = 0;
                while (p < val.size()) {
                    size_t c = val.find(',', p);
                    if (c == std::string::npos) c = val.size();
                    Source s;
                    if (ParseSource(Trim(val.substr(p, c - p)).c_str(), s)) dev->out[o].push_back(s);
                    p = c + 1;
                }
            }
        }
    }
}

inline std::string WriteConfig(const InputConfig& cfg)
{
    std::string t = "; TTT2 Client controller mapping (written by the client; see its Controls page)\n[general]\n";
    t += std::string("socd_lr = ") + (cfg.socdLR == LR_LAST ? "last" : cfg.socdLR == LR_OFF ? "off" : "neutral") + "\n";
    t += std::string("socd_ud = ") + (cfg.socdUD == UD_NEUTRAL ? "neutral" : cfg.socdUD == UD_LAST ? "last" : cfg.socdUD == UD_OFF ? "off" : "up") + "\n";
    t += std::string("stick = ") + (cfg.stick == STICK_DPAD ? "dpad" : cfg.stick == STICK_OFF ? "off" : "analog") + "\n";
    t += "deadzone = " + std::to_string(cfg.deadzone) + "\n";
    for (auto& d : cfg.devices) {
        if (d.empty()) continue;
        t += "\n[device " + d.guid + "]\nname = " + d.name + "\n";
        for (int o = 0; o < O_COUNT; ++o) {
            if (d.out[o].empty()) continue;
            t += std::string(OutKey(o)) + " = ";
            for (size_t i = 0; i < d.out[o].size(); ++i) t += (i ? "," : "") + SourceText(d.out[o][i]);
            t += "\n";
        }
    }
    return t;
}

// SOCD: which directions survive when opposite ones are held together. lastH / lastV: the most recent
// horizontal / vertical press (O_LEFT/O_RIGHT, O_UP/O_DOWN), kept by the caller.
inline uint16_t CleanSocd(uint16_t b, int lr, int ud, int lastH, int lastV)
{
    const uint16_t L = OutBit(O_LEFT), R = OutBit(O_RIGHT), U = OutBit(O_UP), D = OutBit(O_DOWN);
    if ((b & L) && (b & R)) {
        if (lr == LR_NEUTRAL) b &= ~(L | R);
        else if (lr == LR_LAST) b &= ~(lastH == O_LEFT ? R : L);
    }
    if ((b & U) && (b & D)) {
        if (ud == UD_UP) b &= ~D;
        else if (ud == UD_NEUTRAL) b &= ~(U | D);
        else if (ud == UD_LAST) b &= ~(lastV == O_UP ? D : U);
    }
    return b;
}

// Client -> controller layer: a named shared memory block (Local\TTT2ClientPads). The client can
// press buttons for a player (menu automation), and the layer reports what it sees.
struct SharedPads {
    static constexpr uint32_t kMagic = 0x32545454;   // "TTT2"
    uint32_t magic;
    uint32_t version;            // 1
    volatile uint16_t inject[4]; // buttons the client holds for each player slot (ORed in)
    volatile uint32_t layerAlive;// the layer's heartbeat (ms tick) while the game runs
    volatile uint32_t connected; // bit per player slot with a controller
    char names[4][64];           // each slot's controller name
};
inline const wchar_t* SharedPadsName() { return L"Local\\TTT2ClientPads"; }

} // namespace ttt2
