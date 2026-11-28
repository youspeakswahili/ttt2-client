// TTT2 Client - HOME: the game's state and the way in (play, online, practice), and what still needs doing.
#include "app_impl.h"

#include <cstdio>

using namespace ui;

void App::Impl::PageHome()
{
    // the hero: a banner with slashes in the accent
    const float bw = ImGui::GetContentRegionAvail().x, bh = 240;
    const ImVec2 p = ImGui::GetCursorScreenPos();
    ImDrawList* d = ImGui::GetWindowDrawList();
    d->AddRectFilled(p, { p.x + bw, p.y + bh }, col::card, 14.f);
    d->PushClipRect(p, { p.x + bw, p.y + bh }, true);
    for (int i = 0; i < 7; ++i) {
        const float x = p.x + bw * 0.52f + float(i) * 58.f, k = 120.f;
        const ImU32 c = i % 3 == 1 ? Alpha(col::red, 0.06f + 0.012f * float(i)) : Alpha(col::accent, 0.05f + 0.015f * float(i));
        d->AddQuadFilled({ x + k, p.y }, { x + k + 26, p.y }, { x + 26, p.y + bh }, { x, p.y + bh }, c);
    }
    d->AddRectFilledMultiColor(p, { p.x + bw * 0.7f, p.y + bh }, col::card, Alpha(col::card, 0.f), Alpha(col::card, 0.f), col::card);
    d->PopClipRect();
    d->AddRect(p, { p.x + bw, p.y + bh }, col::line, 14.f);

    ImGui::SetCursorScreenPos({ p.x + 36, p.y + 30 });
    ImGui::BeginGroup();
    if (!installed) Tag("SETUP NEEDED", col::amber);
    else if (gameRunning) Tag("IN GAME", col::blue);
    else if (buildOutdated) Tag("UPDATE READY", col::accent);
    else Tag("READY", col::green);
    Gap(2);
    Text(F.heavy, 62.f, col::text, "FIGHT");
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - 8);
    Text(F.body, 14.f, col::dim, installed ? ("Installed in " + installDir).c_str() : "Build the game from your own disc image to start. It takes a few minutes.");
    Gap(16);
    if (!installed) {
        if (BigButton("SET UP THE GAME", { 300, 54 })) page = SETUP;
    } else {
        if (BigButton(gameRunning ? "RUNNING" : "PLAY", { 220, 54 }, !gameRunning)) LaunchGame();
        ImGui::SameLine(0, 14);
        if (BigButton("ONLINE", { 200, 54 }, true, col::red)) page = ONLINE;
    }
    if (!launchNote.empty()) { Gap(4); Small(col::amber, launchNote.c_str()); }
    ImGui::EndGroup();
    ImGui::SetCursorScreenPos({ p.x, p.y + bh + 18 });

    // left: what's ready; right: online and practice
    const float half = (ImGui::GetContentRegionAvail().x - 18) * 0.5f;
    ImGui::BeginChild("homeleft", { half, 0 }, ImGuiChildFlags_AutoResizeY);
    BeginCard("ready", 0, "Ready check");
    Check(installed, installed ? "Game installed" : "Game not set up", installed ? "Built from your disc image." : "Go to SETUP: you need your disc image and Title Update 3.");
    if (installed && buildOutdated) {
        Check(false, "A newer game build came with this client", "It goes in automatically the next time you play, or now:");
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + 30);
        if (Button(builder.Running() ? "Updating..." : "Update now", { 130, 30 }, !builder.Running() && !gameRunning)) UpdateBuild();
        Gap(4);
    }
    {
        std::string pads;
        for (auto& dv : devices) pads += (pads.empty() ? "" : ", ") + dv.name;
        Check(!devices.empty() || devicesSuspended, devicesSuspended ? "Controllers in use by the game" : devices.empty() ? "No controller connected" : "Controllers connected",
              devicesSuspended ? "" : pads.empty() ? "Plug one in, or play on the keyboard." : pads.c_str());
    }
    {
        static const char* q[] = { "", "720p (original)", "1440p", "4K" };
        const std::string g = std::string("Drawn at ") + q[std::clamp(game.GetInt("Graphics", "Scale", 2), 1, 3)] + ", 60 fps. Change it in SETTINGS.";
        Check(true, "PC graphics", g.c_str());
    }
    {
        const std::string mode = client.Get("voice", "mode", "ptt");
        const std::string vk = VkName(client.GetInt("voice", "key", 0x14));
        const std::string detail = mode == "off" ? std::string("Off. Turn it on in SETTINGS > Voice chat.")
                                 : mode == "open" ? std::string("Open mic in online matches.")
                                                  : "Push to talk: hold " + vk + " in online matches.";
        Check(mode != "off", "Voice chat", detail.c_str());
    }
    EndCard();
    ImGui::EndChild();
    ImGui::SameLine(0, 18);
    ImGui::BeginChild("homeright", { half, 0 }, ImGuiChildFlags_AutoResizeY);
    OnlineSummary();
    Gap();
    BeginCard("homepractice", 0, "Practice", "Your team against a training partner, straight into the fight.");
    {
        auto name = [](int id) { return id < 0 || id == roster::kRandom ? std::string("Random") : std::string(roster::CharacterName(id)); };
        const bool solo = client.Get("picks", "team", "duo") == "solo", osolo = client.Get("practice", "oteam", "duo") == "solo";
        std::string me = name(client.GetInt("picks", "c1", 14)), them = name(client.GetInt("practice", "o1", 13));
        if (!solo) me += " & " + name(client.GetInt("picks", "c2", 42));
        if (!osolo) them += " & " + name(client.GetInt("practice", "o2", 9));
        Text(F.bold, 15.f, col::text, (me + "  vs  " + them).c_str());
        Gap(6);
        if (Button(gameProc ? "Game running" : "Start practice", { 160, 34 }, installed && !gameRunning)) StartPractice();
        ImGui::SameLine(0, 10);
        if (Button("Change", { 100, 34 })) page = PRACTICE;
    }
    EndCard();
    ImGui::EndChild();
    Gap();
    BeginCard("homereplays", 0, "Latest replays");
    ReplaysList(3);
    EndCard();
}

// The account and the queue in a few lines, with the way to the ONLINE page (HOME's right column).
void App::Impl::OnlineSummary()
{
    BeginCard("onlinesum", 0, "Online");
    const Online::View v = online ? online->Snapshot() : Online::View{};
    char t[200];
    if (v.state == Online::LOBBY) {
        std::snprintf(t, sizeof t, "%s  ·  rating %d  ·  %d online now", v.name.c_str(), v.rating, v.online);
        Text(F.bold, 15.f, col::text, t);
    } else if (v.state == Online::QUEUED) {
        Text(F.bold, 15.f, col::accent, "Searching for a match...");
    } else if (v.state == Online::CHECKING || v.state == Online::READY) {
        std::snprintf(t, sizeof t, "Match found: vs %s", v.match.opponent.c_str());
        Text(F.bold, 15.f, col::green, t);
    } else if (v.state == Online::NEED_NAME) {
        Small(col::dim, "Pick your fighter name to play ranked and casual matches.");
    } else {
        Small(v.state == Online::FAILED ? col::amber : col::dim, v.error.empty() ? "Connecting to the server..." : v.error.c_str());
    }
    Gap(6);
    if (Button(v.state == Online::QUEUED ? "See the search" : v.state == Online::LOBBY ? "Find a match" : "Open ONLINE", { 160, 34 })) page = ONLINE;
    EndCard();
}
