// TTT2 Client - the app's state, shared by its pages (page_*.cpp), the game launcher (game.cpp) and the controller
// view (controls.cpp). One window: a sidebar and six pages, all usable with a controller.
#pragma once
#include "app.h"
#include "builder.h"
#include "ini.h"
#include "net.h"
#include "online.h"
#include "pads.h"
#include "platform.h"
#include "roster.h"
#include "ui.h"
#include "updater.h"
#include "version.h"
#include "../common/input_config.h"

#include <SDL3/SDL.h>
#include "imgui.h"

#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

using Env = std::vector<std::pair<std::string, std::string>>;

struct App::Impl {
    SDL_Window* window = nullptr;
    enum Page { HOME, ONLINE, PRACTICE, CONTROLS, SETTINGS, SETUP, PAGE_COUNT } page = HOME;
    int settingsTab = 0;

    // ------------------------------------------------------------ settings files (%LOCALAPPDATA%\TTT2Client)
    IniFile client;   // client.ini: the client's own choices, voice chat, picks, the queue
    IniFile game;     // game.ini: the game's graphics and gameplay settings
    void Load();
    void SaveClient();
    void SaveGame();
    void Note(const std::string& s);   // a short message at the bottom of the window
    std::string note;
    double noteAt = -10;

    // ------------------------------------------------------------ the client window
    bool fullscreen = false;
    void SetFullscreen(bool on);

    // ------------------------------------------------------------ the game install
    std::string installDir;
    bool installed = false, gameRunning = false, buildOutdated = false;
    double lastProbe = -10;
    void Probe();
    void CheckBuild();     // the installed game build against the one that came with this client
    void UpdateBuild();    // copy this client's game build into the install
    Builder builder;
    std::string iso, tu, dest;
    std::mutex dialogLock;
    std::string* dialogTarget = nullptr;
    std::string dialogResult;
    bool dialogDone = false;
    void Pick(std::string* target, bool folder, bool isoFilter);
    void PollDialogs();

    // ------------------------------------------------------------ the client's own updates
    Updater updater;
    bool wantsQuit = false, justUpdated = false, autoInstall = false;
    std::string updateNote;
    void InstallUpdate();
    void UpdateBanner();

    // ------------------------------------------------------------ controllers (controls.cpp)
    std::vector<Device> devices;
    std::string selectedGuid;
    ttt2::InputConfig input;
    int captureOut = -1;
    bool captureAppend = false, captureWaitRelease = true, wizard = false;
    int wizardIndex = 0;
    double captureStart = 0;
    std::vector<Sint16> baseline;
    bool devicesSuspended = false;
    void SaveInput();
    void SyncDeviceHold();
    void AddDevice(SDL_JoystickID id);
    void RemoveDevice(SDL_JoystickID id);
    Device* Selected();
    void StartCapture(int out, bool append);
    void StopCapture();
    void UpdateCapture();
    void SeedFromGamepad(const Device& d, ttt2::DeviceMap& m);
    bool OutHeld(const Device& d, int out);
    bool ShowAsStick(const Device& d);
    bool PictureHeld(const Device& d, int out);
    void DrawPadPicture(Device* dv, ImVec2 p, float w, float height);
    void Visualizer(Device* d, float height);

    // ------------------------------------------------------------ the game (game.cpp)
    uintptr_t gameProc = 0;          // the game started from HOME / PRACTICE / a replay
    std::string launchNote;
    bool BringBuild(std::string& note);   // the client's game build into the install before a start
    Env CommonEnv();
    Env GraphicsEnv();
    bool Launch(Env env, uintptr_t& proc, std::string& note);
    void LaunchGame();
    void StartPractice();
    void StartReplay(const std::string& file);
    static std::string ReplaysDir() { return plat::JoinPath(plat::ClientDataDir(), "replays"); }
    // live settings the running game re-reads (volume, voice chat)
    bool liveDirty = false;
    double liveWrittenAt = 0;
    void WriteLive();
    // the voice chat's devices
    std::vector<plat::AudioDevice> mics, outs;
    std::string defMic, defOut;
    double audioListAt = -100;
    void RefreshAudio();
    bool AudioCombo(const char* id, const char* key, bool microphone, float width);
    // a key chosen by pressing it (push to talk, mute)
    bool keyListen = false;
    std::string keyTarget = "key";
    bool keyHeld[256] = {};
    int keyCooldown = 0;
    void KeyBindRow(const char* target, const char* title, const char* hint, int def);
    void StartKeyListen(const char* target);
    void StopKeyListen();
    void PollKeyListen();
    const char* GameKeyClash(int vk);

    // ------------------------------------------------------------ online (game.cpp, page_online.cpp)
    std::unique_ptr<Online> online;
    int qMode = 0, qSide = 0, qPing = 2;
    static constexpr int kPings[5] = { 60, 90, 120, 150, 200 };
    char nameBuf[32] = {};
    char renameBuf[32] = {};
    bool renaming = false;
    double lastBoard = -100;
    void StartOnline();
    void SaveQueuePrefs();
    // the match handed to the game: its process, which match, its result
    uintptr_t matchProc = 0;
    std::string matchStarted, matchNote, matchTeam;
    struct SetScore { int won = 0, lost = 0, draws = 0; int Games() const { return won + lost + draws; } };
    SetScore setScore;
    std::string resultsFor;
    bool resultSent = false, noResult = false;
    std::string lastSet, lastSetFor;
    double scoreReadAt = 0;
    MatchInfo resultFor;
    static std::string ResultsFile() { return plat::JoinPath(plat::ClientDataDir(), "match_results.txt"); }
    static SetScore ReadScore();
    std::string FightsJson();
    bool StartMatch(const MatchInfo& m);
    void MatchGameClosed();
    void TickMatch();
    // ranks (the game's own, kept by the server)
    int gameRank[64];
    bool ranksKnown = false;
    int serverCharsVersion = -1;
    std::string serverRanksFor;
    struct RankChange { int character = -1, before = 0, after = 0, pointsBefore = 0, pointsAfter = 0; };
    std::vector<RankChange> lastRanks;
    void TakeServerRanks();
    // accepting a found match (also asked in the game while practising)
    std::string promptFor;
    bool promptInGame = false, acceptNavOff = false;
    void TickAccept();
    void WritePrompt(const Online::View* v);
    // the test logs: every match's results and the game's log, in one file
    std::string logStamp;
    static std::string TestLogsDir() { return plat::JoinPath(plat::ClientDataDir(), "logs"); }
    void SaveMatchLog();

    // ------------------------------------------------------------ pages
    void Frame();
    void Sidebar(float w, float h);
    void PageHome();
    void PageOnline();
    void PagePractice();
    void PageControls();
    void PageSettings();
    void PageSetup();
    // pieces used on more than one page
    bool CharacterCombo(const char* id, const char* section, const char* key, bool allowRandom, float width = 205);
    void TeamRows(float ctl);
    void PracticeRows(float ctl);
    void ReplaysList(int max);
    void OnlineSummary();
};
