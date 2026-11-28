// TTT2 Client - SETUP: building the game from the player's own disc image, the game build, and this client's version.
#include "app_impl.h"

#include <cstdio>

using namespace ui;

void App::Impl::PageSetup()
{
    PageHeader("SETUP", "The PC version is built from your own Xbox 360 disc. The game build comes with the client; your disc supplies the game.");
    builder.Poll();
    PollDialogs();
    const bool busy = builder.Running();

    if (installed) {
        BeginCard("current", 0, "Installed");
        Text(F.bold, 15.f, col::text, installDir.c_str());
        Small(buildOutdated ? col::accent : col::dim, buildOutdated ? "This client has a newer game build: it goes in the next time you play, or now."
                                                                    : "The game build is the one that came with this client.");
        Gap(6);
        if (Button(buildOutdated ? "Update the game build" : "Reinstall the game build", { 230, 34 }, !busy && !gameRunning)) UpdateBuild();
        ImGui::SameLine(0, 10);
        if (Button("Open folder", { 130, 34 })) plat::OpenFolder(installDir);
        if (gameRunning) Small(col::amber, "Close the game to change its files.");
        EndCard();
        Gap();
    }

    BeginCard("build", 0, installed ? "Build again" : "Build the game",
              "You need your own Tekken Tag Tournament 2 (USA, Europe) disc image and its Title Update 3. No game files come with the client: "
              "they're copied out of your disc image on this PC (about 8 GB).");
    auto field = [&](const char* label, const char* help, std::string& value, const char* id, bool folder, bool isoFilter, bool alsoFolder) {
        ImGui::PushID(id);
        Text(F.bold, 14.f, col::text, label);
        if (help) Small(col::faint, help);
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
        Gap(8);
        ImGui::PopID();
    };
    field("Disc image (.iso)", "The whole disc, about 7.8 GB. If it's in a .zip or .7z, extract it first.", iso, "iso", false, true, false);
    field("Title Update 3", "The file TU_174Q22P_0000008000000.00000000000O2, or the folder it's in.", tu, "tu", false, false, true);
    field("Install folder", "Not inside Program Files, and best not inside OneDrive (it would upload 8 GB).", dest, "dest", true, false, false);
    const uint64_t free = plat::FreeBytes(dest);
    if (free) {
        char t[96];
        std::snprintf(t, sizeof t, "%.0f GB free on that drive", double(free) / double(1ull << 30));
        Text(F.body, 12.5f, free < (9ull << 30) ? col::red : col::faint, t);
    }
    Gap(8);
    const bool ready = ((!iso.empty() && !tu.empty()) || Builder::Extracted(dest)) && !dest.empty() && !busy && !gameRunning;
    if (BigButton(busy ? "BUILDING..." : installed ? "BUILD AGAIN" : "BUILD THE GAME", { 300, 54 }, ready)) {
        BuildRequest r{ iso, tu, dest, {}, false };
        SaveClient();
        builder.Start(r);
    }
    EndCard();

    if (busy || builder.Finished() || !builder.Log().empty()) {
        Gap();
        BeginCard("progress");
        Text(F.display, 22.f, col::text, busy ? "Building" : builder.Succeeded() ? "Finished" : "Stopped");
        ImGui::PushStyleColor(ImGuiCol_PlotHistogram, V(builder.Finished() && !builder.Succeeded() ? col::red : col::accent));
        ImGui::PushStyleColor(ImGuiCol_FrameBg, V(col::cardHi));
        ImGui::ProgressBar(builder.Progress(), { -1, 8 }, "");
        ImGui::PopStyleColor(2);
        Gap(4);
        Text(F.bold, 14.f, col::text, builder.Status().c_str());
        ImGui::PushStyleColor(ImGuiCol_ChildBg, V(IM_COL32(12, 12, 15, 255)));
        ImGui::BeginChild("log", { 0, 150 }, ImGuiChildFlags_Borders);
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
            SaveClient();
            Probe();
            CheckBuild();
        }
    }

    Gap();
    BeginCard("clientver", 0, "This client");
    {
        std::string v = std::string("TTT2 Client ") + TTT2_CLIENT_VERSION;
        const Updater::State st = updater.GetState();
        if (st == Updater::UP_TO_DATE) v += "  ·  up to date";
        else if (st == Updater::AVAILABLE || st == Updater::READY) v += "  ·  " + updater.Latest() + " is out (see the top of the page)";
        else if (st == Updater::FAILED) v += "  ·  couldn't check: " + updater.Error();
        Text(F.body, 14.f, col::text, v.c_str());
        Gap(4);
        if (Button(st == Updater::CHECKING ? "Checking..." : "Check for updates", { 180, 34 }, st != Updater::CHECKING && st != Updater::DOWNLOADING)) updater.Check();
        ImGui::SameLine(0, 10);
        if (Button("Releases page", { 150, 34 })) plat::OpenUrl(std::string("https://github.com/") + Updater::kRepo + "/releases");
        ImGui::SameLine(0, 10);
        if (Button("Settings folder", { 150, 34 })) plat::OpenFolder(plat::ClientDataDir());
    }
    Gap(6);
    Small(col::faint, "Not affiliated with Bandai Namco or Microsoft. You need to own the game.");
    EndCard();
}
