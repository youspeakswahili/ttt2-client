// TTT2 Client - the window: a sidebar and six pages (HOME, ONLINE, PRACTICE, CONTROLS, SETTINGS, SETUP), all usable with
// a controller (D-pad / stick to move, A to choose). The pages: page_*.cpp; starting the game: game.cpp; controllers:
// controls.cpp; the look: ui.cpp.
#include "app_impl.h"
#include "imgui_internal.h"

#include <cstdio>
#include <cstring>

using namespace ui;

// ---------------------------------------------------------------- settings files
void App::Impl::Load()
{
    std::string t;
    std::fill(std::begin(gameRank), std::end(gameRank), -1);
    if (plat::ReadText(plat::JoinPath(plat::ClientDataDir(), "client.ini"), t)) client.Parse(t);
    if (plat::ReadText(plat::JoinPath(plat::ClientDataDir(), "game.ini"), t)) game.Parse(t);
    // Picks saved by clients before 0.4.0's renumbering: the same characters by the game's own numbers
    if (client.GetInt("picks", "numbers", 1) < 2) {
        static constexpr int kOldToNew[64] = { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 19, 20, 21, 22, 23, 24, 25, 26,
                                               27, 28, 29, 30, 31, 32, 43, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 44, 45, 46, 47, 48, 50, 51,
                                               52, 53, 54, 55, 56, 57, 58, 49, 18, -1, -1, -1, -1, 63 };
        for (const auto& [section, key] : { std::pair{ "picks", "c1" }, { "picks", "c2" }, { "practice", "o1" }, { "practice", "o2" } }) {
            const int was = client.GetInt(section, key, -1);
            if (was >= 0 && was < 64 && kOldToNew[was] >= 0) client.Set(section, key, std::to_string(kOldToNew[was]));
        }
        client.Set("picks", "numbers", "2");
    }
    // Settings older clients kept elsewhere (0.5.0-alpha moved them): the game volume, the overlay, the unlocks
    if (client.Get("sound", "volume", "").empty() && !client.Get("client", "volume", "").empty()) client.Set("sound", "volume", client.Get("client", "volume"));
    if (game.Get("Game", "Overlay", "").empty() && !client.Get("client", "netstatus", "").empty()) game.Set("Game", "Overlay", client.Get("client", "netstatus"));
    if (game.Get("Game", "UnlockAll", "").empty() && !client.Get("client", "unlockall", "").empty()) game.Set("Game", "UnlockAll", client.Get("client", "unlockall"));
    if (game.GetInt("Graphics", "Scale", 2) > 3) game.SetInt("Graphics", "Scale", 3);
    if (client.Get("picks", "team", "duo") == "pair") client.Set("picks", "team", "duo");   // pair play is gone
    installDir = client.Get("client", "install_dir");
    iso = client.Get("client", "iso");
    tu = client.Get("client", "tu");
    fullscreen = client.GetInt("client", "fullscreen", 0) != 0;
    if (fullscreen) SDL_SetWindowFullscreen(window, true);
    dest = plat::LooksLikeInstall(installDir) ? installDir : (installDir.empty() ? plat::DefaultInstallDir() : installDir);
    if (plat::ReadText(plat::JoinPath(plat::ClientDataDir(), "input.ini"), t)) ttt2::ParseConfig(t, input);
    SaveClient();
    SaveGame();
    Probe();
    CheckBuild();
    Updater::CleanUpOldFiles();
    updater.Check();
    RefreshAudio();
    if (!std::getenv("TTT2_NO_ONLINE")) StartOnline();
}
void App::Impl::SaveClient()
{
    client.Set("client", "install_dir", installDir);
    client.Set("client", "iso", iso);
    client.Set("client", "tu", tu);
    plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "client.ini"), client.Text());
}
void App::Impl::SaveGame()
{
    if (!plat::WriteText(plat::JoinPath(plat::ClientDataDir(), "game.ini"), game.Text())) Note("Could not save the game's settings.");
}
void App::Impl::Note(const std::string& s)
{
    note = s;
    noteAt = SDL_GetTicks() / 1000.0;
}
void App::Impl::SetFullscreen(bool on)
{
    fullscreen = on;
    SDL_SetWindowFullscreen(window, on);
    client.Set("client", "fullscreen", on ? "1" : "0");
    SaveClient();
}

// ---------------------------------------------------------------- the install
void App::Impl::Probe()
{
    installed = plat::LooksLikeInstall(installDir);
    gameRunning = gameProc != 0 || matchProc != 0 || plat::IsGameRunning();
}
void App::Impl::CheckBuild()
{
    std::string mine, theirs;
    const std::string engine = plat::JoinPath(Builder::EngineDir(), plat::kGameExe);
    buildOutdated = installed && plat::ReadBinary(engine, mine) && plat::ReadBinary(plat::JoinPath(installDir, plat::kGameExe), theirs) &&
                    mine != theirs;
}
void App::Impl::UpdateBuild()
{
    BuildRequest r;
    r.dest = installDir;
    r.onlyLayer = true;
    builder.Start(r);
}
void App::Impl::Pick(std::string* target, bool folder, bool isoFilter)
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
    static const SDL_DialogFileFilter isoFilters[] = { { "Xbox 360 disc image", "iso" }, { "All files", "*" } };
    static const SDL_DialogFileFilter anyFilter[] = { { "All files", "*" } };
    if (folder) SDL_ShowOpenFolderDialog(cb, this, window, target->empty() ? nullptr : target->c_str(), false);
    else SDL_ShowOpenFileDialog(cb, this, window, isoFilter ? isoFilters : anyFilter, isoFilter ? 2 : 1, target->empty() ? nullptr : target->c_str(), false);
}
void App::Impl::PollDialogs()
{
    std::lock_guard l(dialogLock);
    if (!dialogDone) return;
    dialogDone = false;
    if (dialogTarget && !dialogResult.empty()) { *dialogTarget = dialogResult; SaveClient(); }
    dialogTarget = nullptr;
}

// ---------------------------------------------------------------- the client's own updates
void App::Impl::InstallUpdate()
{
    std::string err;
    if (!updater.Apply(err)) { updateNote = err; return; }
    if (plat::Relaunch("--updated")) wantsQuit = true;
    else updateNote = "Updated. Close and reopen the client to finish.";
}
void App::Impl::UpdateBanner()
{
    if (justUpdated && !builder.Running()) {   // after an update: the install gets the new game build
        justUpdated = false;
        CheckBuild();
        if (buildOutdated && !gameRunning) UpdateBuild();
    }
    if (builder.Finished() && page != SETUP) {   // a build update started from HOME
        builder.Acknowledge();
        Probe();
        CheckBuild();
    }
    if (autoInstall && updater.GetState() == Updater::READY) { autoInstall = false; InstallUpdate(); }
    const Updater::State st = updater.GetState();
    if (st != Updater::AVAILABLE && st != Updater::DOWNLOADING && st != Updater::READY && updateNote.empty()) return;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, V(Alpha(col::accent, 0.10f)));
    ImGui::PushStyleColor(ImGuiCol_Border, V(Alpha(col::accent, 0.45f)));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 12.f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 20, 14 });
    ImGui::BeginChild("update", { 0, 0 }, ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding);
    std::string title, sub;
    if (!updateNote.empty()) { title = "Update"; sub = updateNote; }
    else if (st == Updater::AVAILABLE) { title = "TTT2 Client " + updater.Latest() + " is out"; sub = std::string("You have ") + TTT2_CLIENT_VERSION + ". One click: it downloads, installs and restarts."; }
    else if (st == Updater::DOWNLOADING) { title = "Updating to " + updater.Latest(); sub = "Downloading and checking it. The client restarts by itself."; }
    else { title = "Installing " + updater.Latest(); sub = "Restarting..."; }
    ImGui::BeginGroup();
    Text(F.display, 20.f, col::text, title.c_str());
    Small(col::dim, sub.c_str());
    if (st == Updater::DOWNLOADING) {
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, V(col::accent));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, V(col::cardHi));
        ImGui::ProgressBar(updater.Progress(), { 300, 6 }, "");
        ImGui::PopStyleColor(2);
    }
    ImGui::EndGroup();
    if (updateNote.empty() && st == Updater::AVAILABLE) {
        ImGui::SameLine(ImGui::GetContentRegionAvail().x - 160 + ImGui::GetStyle().WindowPadding.x);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 6);
        if (BigButton("UPDATE", { 160, 40 })) { autoInstall = true; updater.Download(); }
    }
    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor(2);
    Gap();
}

// ---------------------------------------------------------------- the sidebar
void App::Impl::Sidebar(float w, float h)
{
    ImDrawList* d = ImGui::GetWindowDrawList();
    const ImVec2 o = ImGui::GetWindowPos();
    d->AddRectFilled(o, { o.x + w, o.y + h }, col::side);
    d->AddLine({ o.x + w, o.y }, { o.x + w, o.y + h }, col::line);
    // the mark and the client's name
    const ImVec2 m = { o.x + 18, o.y + 18 };
    if (g_mark) d->AddImage(ImTextureRef(reinterpret_cast<ImTextureID>(g_mark)), m, { m.x + 60, m.y + 60 });
    ImGui::PushFont(F.heavy, 29.f);
    d->AddText({ m.x + 70, m.y + 6 }, col::text, "TTT2 CLIENT");
    ImGui::PopFont();
    ImGui::PushFont(F.bold, 11.5f);
    d->AddText({ m.x + 72, m.y + 39 }, col::accent, "TEKKEN TAG TOURNAMENT 2");
    ImGui::PopFont();

    ImGui::SetCursorPos({ 0, 100 });
    static const char* names[] = { "HOME", "ONLINE", "PRACTICE", "CONTROLS", "SETTINGS", "SETUP" };
    const Online::View ov = online ? online->Snapshot() : Online::View{};
    for (int i = 0; i < PAGE_COUNT; ++i) {
        ImGui::PushID(i);
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const ImVec2 size = { w - 1, 48 };
        if (ImGui::InvisibleButton("nav", size)) page = Page(i);
        const bool active = page == i, hot = ImGui::IsItemHovered() || ImGui::IsItemFocused();
        if (active) {
            d->AddRectFilledMultiColor(p, { p.x + size.x, p.y + size.y }, Alpha(col::accent, 0.16f), Alpha(col::accent, 0.0f),
                                       Alpha(col::accent, 0.0f), Alpha(col::accent, 0.16f));
            d->AddRectFilled(p, { p.x + 4, p.y + size.y }, col::accent);
        } else if (hot) {
            d->AddRectFilled(p, { p.x + size.x, p.y + size.y }, Alpha(col::text, 0.04f));
        }
        ImGui::PushFont(F.display, 23.f);
        d->AddText({ p.x + 30, p.y + 11 }, active || hot ? col::text : col::dim, names[i]);
        ImGui::PopFont();
        // a dot where something waits: the setup not done, a match being searched or found
        if (i == SETUP && (!installed || buildOutdated)) d->AddCircleFilled({ p.x + w - 30, p.y + 24 }, 4.5f, col::amber);
        if (i == ONLINE && (ov.state == Online::QUEUED || ov.state == Online::CHECKING || ov.state == Online::READY))
            d->AddCircleFilled({ p.x + w - 30, p.y + 24 }, 4.5f, ov.state == Online::QUEUED ? col::accent : col::green);
        ImGui::PopID();
    }

    // bottom: status, then the client's version
    ImGui::SetCursorPos({ 26, h - 196 });
    ImGui::BeginGroup();
    auto status = [&](const char* head, const char* value, ImU32 c) {
        Text(F.bold, 11.f, col::faint, head);
        Text(F.bold, 14.f, c, value);
        ImGui::Dummy({ 0, 4 });
    };
    status("GAME", installed ? (gameRunning ? "Running" : "Ready") : "Not set up", installed ? col::text : col::amber);
    {
        char t[64];
        std::snprintf(t, sizeof t, devicesSuspended ? "In use by the game" : devices.empty() ? "None (keyboard works)" : "%zu connected", devices.size());
        status("CONTROLLERS", t, col::text);
    }
    {
        std::string s = ov.state == Online::LOBBY || ov.state == Online::QUEUED || ov.state == Online::CHECKING || ov.state == Online::READY
                            ? ov.name + "  ·  " + std::to_string(ov.online) + " online"
                            : ov.state == Online::NEED_NAME ? std::string("Pick a name") : ov.state == Online::FAILED ? std::string("Can't reach the server") : std::string("Connecting...");
        status("SERVER", s.c_str(), ov.state == Online::FAILED ? col::amber : col::text);
    }
    ImGui::EndGroup();
    ImGui::SetCursorPos({ 26, h - 34 });
    {
        const Updater::State st = updater.GetState();
        std::string v = std::string("v") + TTT2_CLIENT_VERSION;
        ImU32 c = col::faint;
        if (st == Updater::AVAILABLE || st == Updater::DOWNLOADING || st == Updater::READY) { v += "  ·  " + updater.Latest() + " is out"; c = col::accent; }
        Text(F.bold, 11.5f, c, v.c_str());
    }
}

// ---------------------------------------------------------------- one frame
void App::Impl::Frame()
{
    const double now = SDL_GetTicks() / 1000.0;
    if (now - lastProbe > 2.0) { lastProbe = now; Probe(); }
    SyncDeviceHold();
    TickMatch();
    TickAccept();
    if (keyListen && page != SETTINGS) StopKeyListen();
    PollKeyListen();
    if (liveDirty && now - liveWrittenAt > 0.1) WriteLive();   // a volume being dragged: the game follows it live

    if (ImGui::IsKeyPressed(ImGuiKey_F11, false) || (ImGui::IsKeyPressed(ImGuiKey_Enter, false) && ImGui::GetIO().KeyAlt)) SetFullscreen(!fullscreen);
    ImGui::GetCurrentContext()->ConfigNavWindowingWithGamepad = false;   // X / Square held: no window switcher

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->Pos);
    ImGui::SetNextWindowSize(vp->Size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0, 0 });
    ImGui::PushStyleColor(ImGuiCol_WindowBg, V(col::bg));
    ImGui::Begin("root", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoBringToFrontOnFocus);
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    const float sideW = 260;
    ImGui::BeginChild("side", { sideW, vp->Size.y }, 0, ImGuiWindowFlags_NoScrollbar);
    Sidebar(sideW, vp->Size.y);
    ImGui::EndChild();
    ImGui::SameLine(0, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 44, 34 });
    ImGui::BeginChild("main", { 0, vp->Size.y }, ImGuiChildFlags_AlwaysUseWindowPadding);
    UpdateBanner();
    switch (page) {
    case HOME: PageHome(); break;
    case ONLINE: PageOnline(); break;
    case PRACTICE: PagePractice(); break;
    case CONTROLS: PageControls(); break;
    case SETTINGS: PageSettings(); break;
    case SETUP: PageSetup(); break;
    default: break;
    }
    Gap(20);
    ImGui::EndChild();
    ImGui::PopStyleVar();

    // a short note at the bottom (saved, errors)
    if (!note.empty() && now - noteAt < 3.0) {
        ImDrawList* d = ImGui::GetForegroundDrawList();
        ImGui::PushFont(F.bold, 14.f);
        const ImVec2 ts = ImGui::CalcTextSize(note.c_str());
        const ImVec2 p = { vp->Pos.x + sideW + (vp->Size.x - sideW - ts.x) * 0.5f - 18, vp->Pos.y + vp->Size.y - 64 };
        d->AddRectFilled(p, { p.x + ts.x + 36, p.y + 38 }, col::cardHi, 19.f);
        d->AddRect(p, { p.x + ts.x + 36, p.y + 38 }, Alpha(col::accent, 0.5f), 19.f);
        d->AddText({ p.x + 18, p.y + 10 }, col::text, note.c_str());
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
    s.FrameRounding = 7;
    s.GrabRounding = 7;
    s.GrabMinSize = 14;
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
    c[ImGuiCol_SliderGrab] = V(col::accent);
    c[ImGuiCol_SliderGrabActive] = V(col::blue);
    c[ImGuiCol_Header] = V(Alpha(col::accent, 0.2f));
    c[ImGuiCol_HeaderHovered] = V(Alpha(col::accent, 0.3f));
    c[ImGuiCol_HeaderActive] = V(Alpha(col::accent, 0.4f));
    c[ImGuiCol_CheckMark] = V(col::accent);
    c[ImGuiCol_ScrollbarBg] = V(0);
    c[ImGuiCol_ScrollbarGrab] = V(col::line);
    c[ImGuiCol_NavCursor] = V(col::accent);
    c[ImGuiCol_TextSelectedBg] = V(Alpha(col::accent, 0.35f));
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
