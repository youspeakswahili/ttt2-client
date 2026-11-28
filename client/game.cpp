// TTT2 Client - starting the game: its settings as environment variables (CommonEnv, GraphicsEnv), practice, replays,
// online matches and their results, the live settings file the running game re-reads, and the voice chat's devices
// and keys. What each variable does in the game: ttt2-dev, game/src (ttt2_hud.cpp, ttt2_voice.cpp, ttt2_direct.cpp...).
#include "app_impl.h"

#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>

using namespace ui;

namespace {
int64_t UnixMs()
{
    return int64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}
int RandomCharacter()
{
    const int n = int(sizeof roster::kCharacters / sizeof roster::kCharacters[0]);
    return roster::kCharacters[SDL_rand(n)].id;
}
} // namespace

// ---------------------------------------------------------------- the game build in the install
// The game build ships in the client's "game build" folder; the install gets a copy of it, brought up to date before
// every start (so a new client always plays with its own build).
bool App::Impl::BringBuild(std::string& note)
{
    if (!installed) { note = "Set up the game first (SETUP)."; return false; }
    const std::string engine = Builder::EngineDir();
    if (!plat::FileExists(plat::JoinPath(engine, plat::kGameExe)) || plat::JoinPath(engine, "x") == plat::JoinPath(installDir, "x"))
        return true;
    std::string a, b, err;
    for (const std::string& f : plat::ListFilesRecursive(engine)) {
        const std::string rel = f.substr(engine.size() + 1), to = plat::JoinPath(installDir, rel);
        if (plat::ReadBinary(f, a) && plat::ReadBinary(to, b) && a == b) continue;
        const size_t slash = to.find_last_of("\\/");
        if (slash != std::string::npos) plat::MakeDir(to.substr(0, slash));
        if (!plat::CopyFileTo(f, to, err)) { note = "Could not update " + rel + " in the game folder: " + err; return false; }
    }
    buildOutdated = false;
    return true;
}

// ---------------------------------------------------------------- what every start gets
Env App::Impl::CommonEnv()
{
    WriteLive();
    const std::string data = plat::ClientDataDir();
    Env env = {
        // sound: the game's volume at the start; the live file has it (and the voice chat) while it runs
        { "TTT2_MASTER_VOLUME", std::to_string(client.GetInt("sound", "volume", 100)) },
        { "TTT2_VOICE_LIVE", plat::JoinPath(data, "live.ini") },
        // voice chat
        { "TTT2_VOICE", client.Get("voice", "mode", "ptt") },
        { "TTT2_VOICE_KEY", std::to_string(client.GetInt("voice", "key", 0x14)) },
        { "TTT2_MUTE_KEY", std::to_string(client.GetInt("voice", "mute_key", 0)) },
        { "TTT2_VOICE_VOLUME", std::to_string(client.GetInt("voice", "volume", 100)) },
        { "TTT2_MIC_VOLUME", std::to_string(client.GetInt("voice", "mic", 100)) },
        { "TTT2_MIC_DEVICE", client.Get("voice", "mic_device", "") },
        { "TTT2_VOICE_DEVICE", client.Get("voice", "out_device", "") },
        // the in-game overlay and the found-match question
        { "TTT2_NET_STATUS", game.GetInt("Game", "Overlay", 1) ? "1" : "0" },
        { "TTT2_PROMPT", plat::JoinPath(data, "match_prompt.ini") },
        // gameplay
        { "TTT2_UNLOCK_ALL", game.GetInt("Game", "UnlockAll", 1) ? "1" : "0" },
        { "TTT2_INPUT_DELAY", game.GetInt("Game", "InputDelay", 0) ? "2" : "0" },
        // where the game's files are
        { "REX_TITLE_GAME_ROOT", plat::JoinPath(installDir, "game") },
        { "REX_TITLE_UPDATE_ROOT", plat::JoinPath(installDir, "update") },
    };
    const Env g = GraphicsEnv();
    env.insert(env.end(), g.begin(), g.end());
    return env;
}

// The Graphics settings as the game build's own setting names (REX_ + the name, dots as underscores). The frame rate is
// not here: the game is locked at 60, as it was made.
Env App::Impl::GraphicsEnv()
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

bool App::Impl::Launch(Env env, uintptr_t& proc, std::string& note)
{
    note.clear();
    if (!BringBuild(note)) return false;
    std::string err;
    proc = plat::LaunchWithEnv(installDir, plat::kGameExe, env, err);
    if (!proc) { note = "Could not start the game: " + err; return false; }
    gameRunning = true;
    return true;
}

void App::Impl::LaunchGame()
{
    // the game as it is: its own menus, story, customization
    Launch(CommonEnv(), gameProc, launchNote);
}

void App::Impl::StartPractice()
{
    auto pick = [](int id) { return id == roster::kRandom || id < 0 ? RandomCharacter() : id; };
    const bool solo = client.Get("picks", "team", "duo") == "solo", osolo = client.Get("practice", "oteam", "duo") == "solo";
    const int a = pick(client.GetInt("picks", "c1", 14)), b = solo ? a : pick(client.GetInt("picks", "c2", 42));
    const int c = pick(client.GetInt("practice", "o1", 13)), d = osolo ? c : pick(client.GetInt("practice", "o2", 9));
    Env env = CommonEnv();
    env.insert(env.end(), { { "TTT2_BOOT", "practice" },   // the game walks its character select to these picks
                            { "TTT2_FIGHTERS", std::to_string(a) + "," + std::to_string(b) + "," + std::to_string(c) + "," + std::to_string(d) },
                            { "TTT2_STAGE", std::to_string(client.GetInt("practice", "stage", -1)) } });
    Launch(env, gameProc, launchNote);
}

void App::Impl::StartReplay(const std::string& file)
{
    if (gameProc) { launchNote = "Close the game first."; return; }
    Env env = CommonEnv();
    env.insert(env.end(), { { "TTT2_BOOT", "versus" }, { "TTT2_REPLAY", file } });
    Launch(env, gameProc, launchNote);
}

// ---------------------------------------------------------------- the live file the running game follows
// The game re-reads it a few times a second (ttt2_hud.cpp: the volume; ttt2_voice.cpp: voice chat): rewritten whole
// on every change, "end=1" last so a half-written file is never used.
void App::Impl::WriteLive()
{
    const std::string t = "game_volume=" + std::to_string(client.GetInt("sound", "volume", 100)) +
                          "\nmode=" + client.Get("voice", "mode", "ptt") + "\nkey=" + std::to_string(client.GetInt("voice", "key", 0x14)) +
                          "\nmute_key=" + std::to_string(client.GetInt("voice", "mute_key", 0)) +
                          "\nvolume=" + std::to_string(client.GetInt("voice", "volume", 100)) +
                          "\nmic=" + std::to_string(client.GetInt("voice", "mic", 100)) + "\nend=1\n";
    plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "live.ini"), t);
    liveDirty = false;
    liveWrittenAt = SDL_GetTicks() / 1000.0;
}

// ---------------------------------------------------------------- voice chat: devices
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
    const std::string shown = cur.empty() ? defaultLabel
                            : present     ? curName
                                          : (curName.empty() ? std::string("A device") : curName) + " (unplugged: Windows default for now)";
    bool changed = false;
    ImGui::SetNextItemWidth(width);
    ImGui::PushFont(F.bold, 14.f);
    if (ImGui::BeginCombo(id, shown.c_str(), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::IsWindowAppearing()) RefreshAudio();
        if (ImGui::Selectable(defaultLabel.c_str(), cur.empty())) { client.Set("voice", key, ""); client.Set("voice", nameKey, ""); changed = true; }
        for (const plat::AudioDevice& d : list) {
            const std::string label = d.name + "##" + d.id;
            if (ImGui::Selectable(label.c_str(), d.id == cur)) { client.Set("voice", key, d.id); client.Set("voice", nameKey, d.name); changed = true; }
        }
        if (list.empty()) Text(F.body, 13.f, col::faint, microphone ? "No microphone found." : "No sound output found.");
        ImGui::EndCombo();
    }
    ImGui::PopFont();
    if (changed) { SaveClient(); WriteLive(); }
    return changed;
}

// ---------------------------------------------------------------- voice chat: keys
// Which of the game's keyboard controls already uses vk (the game's defaults: hid.keybind_* in the game build).
const char* App::Impl::GameKeyClash(int vk)
{
    struct C { const char* name; int key; };
    static const C controls[] = { { "Up", 0x26 }, { "Down", 0x28 }, { "Left", 0x25 }, { "Right", 0x27 }, { "1 (X)", 'Z' },
                                  { "2 (Y)", 'X' }, { "3 (A)", 'A' }, { "4 (B)", 'S' }, { "Tag (LB)", 'Q' }, { "Start", 0x0D } };
    for (const C& c : controls)
        if (c.key == vk) return c.name;
    return nullptr;
}

void App::Impl::KeyBindRow(const char* target, const char* title, const char* hint, int def)
{
    const float ctl = 380;
    const int vk = client.GetInt("voice", target, def);
    RowLabel(title, hint, ctl);
    const float sideW = 84;
    const bool listening = keyListen && keyTarget == target;
    ImGui::PushID(target);
    if (listening) {
        ImGui::PushStyleColor(ImGuiCol_Button, V(col::accentDeep));
        Button("Press a key...##bind", { ctl - sideW - 8, 34 }, false);
        ImGui::PopStyleColor();
        ImGui::SameLine(0, 8);
        if (Button("Cancel", { sideW, 34 })) StopKeyListen();
    } else {
        const std::string label = (vk ? VkName(vk) : std::string("Not set")) + "##bind";
        if (Button(label.c_str(), { ctl - sideW - 8, 34 }, keyCooldown == 0 && !keyListen)) StartKeyListen(target);
        ImGui::SameLine(0, 8);
        if (Button(def ? "Reset" : "Clear", { sideW, 34 }, vk != def)) {
            client.Set("voice", target, std::to_string(def));
            SaveClient();
            WriteLive();
        }
    }
    ImGui::PopID();
    if (!listening && vk) {
        if (const char* clash = GameKeyClash(vk)) {
            char t[200];
            std::snprintf(t, sizeof t, "%s is also the game's keyboard %s, so pressing it does both. Pick another key.", VkName(vk).c_str(), clash);
            Small(col::amber, t);
        }
    }
}

namespace {
// Keys that can be chosen: not left / right click, not Esc (it cancels), not the Windows keys, Print Screen, IME,
// browser or media keys; the left / right variants of Shift, Ctrl and Alt rather than the shared codes.
bool Bindable(int vk)
{
    switch (vk) {
    case 0x01: case 0x02: case 0x03: case 0x10: case 0x11: case 0x12: case 0x1B: case 0x2C: case 0x5B: case 0x5C:
    case 0x5D: case 0x5F: case 0xE5: case 0xE7: return false;
    default: break;
    }
    if ((vk >= 0x15 && vk <= 0x1A) || (vk >= 0x1C && vk <= 0x1F)) return false;
    if (vk >= 0xA6 && vk <= 0xB7) return false;
    return vk > 0 && vk < 0xFF;
}
} // namespace

void App::Impl::StartKeyListen(const char* target)
{
    keyTarget = target;
    for (int vk = 1; vk < 256; ++vk) keyHeld[vk] = plat::KeyHeld(vk);   // only a fresh press counts
    keyListen = true;
    ImGui::GetIO().ConfigFlags &= ~ImGuiConfigFlags_NavEnableKeyboard;  // Space / Enter must not press the button
}
void App::Impl::StopKeyListen()
{
    keyListen = false;
    keyCooldown = 3;   // a few frames before the keyboard drives the client again
}
void App::Impl::PollKeyListen()
{
    if (!keyListen) {
        if (keyCooldown > 0 && --keyCooldown == 0 && !acceptNavOff) ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        return;
    }
    const bool focused = (SDL_GetWindowFlags(window) & SDL_WINDOW_INPUT_FOCUS) != 0;
    for (int vk = 1; vk < 256; ++vk) {
        const bool now = plat::KeyHeld(vk), was = keyHeld[vk];
        keyHeld[vk] = now;
        if (!focused || !now || was) continue;
        if (vk == 0x1B) { StopKeyListen(); return; }
        if (!Bindable(vk)) continue;
        client.Set("voice", keyTarget, std::to_string(vk));
        SaveClient();
        WriteLive();
        StopKeyListen();
        return;
    }
}

// ---------------------------------------------------------------- online
void App::Impl::StartOnline()
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
void App::Impl::SaveQueuePrefs()
{
    client.SetInt("online", "mode", qMode);
    client.SetInt("online", "side", qSide);
    client.SetInt("online", "ping", qPing);
    SaveClient();
}

// A match found and checked: the game starts straight into the fight. Both games start the same offline versus fight
// (each player's team from here, the same stage) and share their keys from its first frame, with rollback. A Random
// pick is made here, because both games have to be told the same characters.
bool App::Impl::StartMatch(const MatchInfo& m)
{
    matchStarted = m.id;
    if (gameProc) { plat::KillProcess(gameProc); gameProc = 0; }   // practice while searching: the match takes over
    if (!BringBuild(matchNote)) return false;
    const int port = online->HandOffUdp();
    const std::string file = plat::JoinPath(plat::ClientDataDir(), "match.ini");
    const std::string ini = "id=" + m.id + "\nslot=" + (m.side == "p1" ? "0" : "1") + "\npeer=" + m.peerAddr + "\nrelay=" + m.relayAddr +
                            "\ntoken=" + m.relayToken + "\nlocalport=" + std::to_string(port) + "\ndirect=" + (m.direct ? "1" : "0") + "\n";
    if (!plat::WriteText(file, ini)) { online->ReclaimUdp(); matchNote = "Could not write the match file."; return false; }
    const Online::View ov = online->Snapshot();
    Env env = CommonEnv();
    env.insert(env.end(), { { "TTT2_NET", "ttt2net" }, { "TTT2_MATCH", file }, { "TTT2_ROLLBACK", "1" }, { "TTT2_RESULT", ResultsFile() } });
    // Rollback balance: Standard holds this PC's keys back up to 2 frames, Prioritize response up to 1 (the rest is guessed)
    env.push_back({ "TTT2_RB_MAXDELAY", game.GetInt("Online", "Netcode", 0) == 1 ? "1" : "2" });
    plat::MakeDir(ReplaysDir());
    env.push_back({ "TTT2_REPLAY_DIR", ReplaysDir() });
    if (resultsFor != m.id) { plat::DeleteFileAt(ResultsFile()); resultsFor = m.id; }
    setScore = ReadScore();
    resultSent = noResult = false;
    const bool solo = client.Get("picks", "team", "duo") == "solo";
    int c1 = client.GetInt("picks", "c1", 14), c2 = solo ? c1 : client.GetInt("picks", "c2", 42);
    if (c1 == roster::kRandom) c1 = RandomCharacter();
    if (solo) c2 = c1;
    for (int tries = 0; (c2 == roster::kRandom || (!solo && c2 == c1)) && tries < 50; ++tries) c2 = RandomCharacter();
    matchTeam = solo ? std::string(roster::CharacterName(c1)) : std::string(roster::CharacterName(c1)) + " and " + roster::CharacterName(c2);
    env.insert(env.end(), { { "TTT2_BOOT", "versus" }, { "TTT2_FIGHTERS", std::to_string(c1) + "," + std::to_string(c2) },
                            { "TTT2_NAME", ov.name }, { "TTT2_OPP_NAME", m.opponent },
                            { "TTT2_MMR", std::to_string(ov.rating) }, { "TTT2_OPP_MMR", std::to_string(m.opponentRating) } });
    {
        char stamp[40] = "match";
        const std::time_t now = std::time(nullptr);
        if (const std::tm* tm = std::localtime(&now)) std::strftime(stamp, sizeof stamp, "%Y-%m-%d %H.%M.%S", tm);
        logStamp = stamp;
    }
    if (!Launch(env, matchProc, matchNote)) { online->ReclaimUdp(); return false; }
    matchNote.clear();
    resultFor = m;
    return true;
}

// The games the game has written for this match so far (TTT2_RESULT: one line per game).
App::Impl::SetScore App::Impl::ReadScore()
{
    SetScore s;
    std::string text;
    if (!plat::ReadBinary(ResultsFile(), text)) return s;
    size_t at = 0;
    while (at < text.size()) {
        const size_t end = text.find('\n', at);
        if (end == std::string::npos) break;   // a line still being written
        const std::string line = text.substr(at, end - at);
        at = end + 1;
        if (line.find("result=won") != std::string::npos) ++s.won;
        else if (line.find("result=lost") != std::string::npos) ++s.lost;
        else if (line.find("result=draw") != std::string::npos) ++s.draws;
    }
    return s;
}

// This match's fights as the game wrote them ("game=1 result=won winner=0 mine=0 frame=812 teams=14,42,0,0"), for the
// server: it works the ranked results out from them, and counts them only when both PCs' are alike.
std::string App::Impl::FightsJson()
{
    std::string text, out = "[";
    if (!plat::ReadBinary(ResultsFile(), text)) return "[]";
    size_t at = 0;
    while (at < text.size()) {
        const size_t end = text.find('\n', at);
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

// The match's part of the log file the player sends when something went wrong: its results and the game's own log.
// One file (logs\matches.txt), the last 5 matches, 4 MB at most.
void App::Impl::SaveMatchLog()
{
    std::string games, log, newest;
    plat::ReadBinary(ResultsFile(), games);
    for (const std::string& f : plat::ListFilesRecursive(plat::JoinPath(installDir, "logs"))) {
        const size_t slash = f.find_last_of("\\/");
        const std::string name = slash == std::string::npos ? f : f.substr(slash + 1);
        if (name.rfind("ttt2_", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".log") == 0 && f > newest) newest = f;
    }
    if (!newest.empty()) plat::ReadBinary(newest, log);
    if (log.size() > 1600 * 1024) {   // a long log keeps its start and its end
        const size_t cap = 1600 * 1024, head = cap / 4;
        log = log.substr(0, head) + "\r\n.... (cut) ....\r\n" + log.substr(log.size() - (cap - head));
    }
    static const std::string kHead = "\r\n==================== MATCH ";
    const std::string part = kHead + logStamp + " ====================\r\n---- results ----\r\n" +
                             (games.empty() ? std::string("(none)\r\n") : games) + "\r\n---- the game's log ----\r\n" + log;
    plat::MakeDir(TestLogsDir());
    const std::string all = plat::JoinPath(TestLogsDir(), "matches.txt");
    std::string before;
    plat::ReadBinary(all, before);
    std::vector<size_t> starts;
    for (size_t at = before.find(kHead); at != std::string::npos; at = before.find(kHead, at + 1)) starts.push_back(at);
    if (starts.size() >= 5) before.erase(0, starts[starts.size() - 4]);
    while (before.size() + part.size() > 4u * 1024 * 1024) {
        const size_t next = before.find(kHead, 1);
        if (next == std::string::npos) { before.clear(); break; }
        before.erase(0, next);
    }
    plat::WriteText(all, before + part);
}

// The game of a match has closed: its log is kept, and its result goes to the server (nobody is asked).
void App::Impl::MatchGameClosed()
{
    online->ReclaimUdp();
    SaveMatchLog();
    setScore = ReadScore();
    if (setScore.won != setScore.lost) {
        const bool won = setScore.won > setScore.lost;
        online->Report(won, FightsJson());
        resultSent = true;
        char t[160];
        std::snprintf(t, sizeof t, "Last match: you %s %d - %d against %s", won ? "won" : "lost", setScore.won, setScore.lost, resultFor.opponent.c_str());
        lastSet = t;
        lastSetFor = resultFor.id;
    } else {
        noResult = true;
    }
    Probe();
}

void App::Impl::TickMatch()
{
    if (gameProc && plat::ProcessDone(gameProc)) gameProc = 0;
    if (!online) return;
    TakeServerRanks();
    if (matchProc && plat::ProcessDone(matchProc)) {
        matchProc = 0;
        MatchGameClosed();
    } else if (matchProc && SDL_GetTicks() / 1000.0 - scoreReadAt > 1.0) {
        scoreReadAt = SDL_GetTicks() / 1000.0;
        setScore = ReadScore();
    }
    const Online::View v = online->Snapshot();
    if (v.state == Online::READY && !matchProc && !resultSent && !noResult && matchStarted != v.match.id && installed) StartMatch(v.match);
    if (v.state != Online::READY) resultSent = noResult = false;   // the server settled it, or the match was left
}

// The server's ranks of each character (the game's own rank system, kept per account), and a rated match's changes.
void App::Impl::TakeServerRanks()
{
    const Online::View v = online->Snapshot();
    if (v.charsVersion != serverCharsVersion && !v.chars.empty()) {
        serverCharsVersion = v.charsVersion;
        for (size_t c = 0; c < v.chars.size() && c < 64; ++c) gameRank[c] = v.chars[c].rank;
        ranksKnown = true;
    }
    if (!v.lastMatch.empty() && v.lastMatch != serverRanksFor) {
        serverRanksFor = v.lastMatch;
        lastRanks.clear();
        for (const Online::View::RankChange& r : v.lastRanks) lastRanks.push_back({ r.c, r.before, r.after, r.pointsBefore, r.pointsAfter });
    }
}

// A found match waiting for this player's answer: a sound and a taskbar flash once, the question in the game when they
// are in it (TTT2_PROMPT, ttt2_hud.cpp), and their answer from there. The client's own pad and keyboard navigation pause
// while the question is up: A / B / Enter / Esc answer it instead.
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
        if (gameProc) { WritePrompt(&v); promptInGame = true; }
    }
    if (asking && promptInGame) {
        std::string t;
        const std::string answer = plat::JoinPath(plat::ClientDataDir(), "match_prompt.ini.answer");
        if (plat::ReadText(answer, t) && t.find("end=1") != std::string::npos && t.find("id=" + v.match.id + "\n") != std::string::npos) {
            if (t.find("answer=accept") != std::string::npos) online->Accept();
            else online->Decline();
            plat::WriteText(answer, "");
        }
    }
    if (!asking && promptInGame) { WritePrompt(nullptr); promptInGame = false; }
    if (asking && !gameProc) promptInGame = false;
    ImGuiIO& io = ImGui::GetIO();
    if (asking && !acceptNavOff) { io.ConfigFlags &= ~(ImGuiConfigFlags_NavEnableGamepad | ImGuiConfigFlags_NavEnableKeyboard); acceptNavOff = true; }
    if (!asking && acceptNavOff) {
        io.ConfigFlags |= ImGuiConfigFlags_NavEnableGamepad;
        if (!keyListen && keyCooldown == 0) io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
        acceptNavOff = false;
    }
}

void App::Impl::WritePrompt(const Online::View* v)
{
    const std::string file = plat::JoinPath(plat::ClientDataDir(), "match_prompt.ini");
    if (!v) { plat::WriteText(file, "show=0\nend=1\n"); return; }
    const int64_t leftMs = std::max<int64_t>(0, int64_t(v->acceptDeadlineMs) - int64_t(net::NowMs()));
    const std::string line = "vs " + v->match.opponent + " (" + std::to_string(v->match.opponentRating) + ")  -  " +
                             (v->match.mode == "ranked" ? "Ranked" : "Casual");
    plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "match_prompt.ini.answer"), "");
    plat::WriteText(file, "id=" + v->match.id + "\nline=" + line + "\ndeadline=" + std::to_string(UnixMs() + leftMs) + "\nshow=1\nend=1\n");
}
