// TTT2 Client - PRACTICE: your team against a training partner, and the replays of your online fights.
#include "app_impl.h"

#include <cstdio>

using namespace ui;

void App::Impl::PracticeRows(float ctl)
{
    TeamRows(ctl);
    Divider();
    static const char* teams[] = { "Tag (2 characters)", "Solo (1 character)" };
    int t = client.Get("practice", "oteam", "duo") == "solo" ? 1 : 0;
    RowLabel("Training partner", "Who you practise against.", ctl);
    if (Segmented("oteam", &t, teams, 2, ctl)) { client.Set("practice", "oteam", t ? "solo" : "duo"); SaveClient(); }
    Gap(4);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ctl);
    const float w = t ? ctl : (ctl - 10) * 0.5f;
    CharacterCombo("o1", "practice", "o1", true, w);
    if (!t) { ImGui::SameLine(0, 10); CharacterCombo("o2", "practice", "o2", true, w); }
    Divider();
    RowLabel("Stage", nullptr, ctl);
    const int stage = client.GetInt("practice", "stage", -1);
    ImGui::SetNextItemWidth(ctl);
    ImGui::PushFont(F.bold, 14.f);
    if (ImGui::BeginCombo("##stage", stage < 0 ? "Random" : roster::StageName(stage), ImGuiComboFlags_HeightLarge)) {
        if (ImGui::Selectable("Random", stage < 0)) { client.Set("practice", "stage", "-1"); SaveClient(); }
        for (const roster::Stage& st : roster::kStages)
            if (ImGui::Selectable(st.name, st.id == stage)) { client.Set("practice", "stage", std::to_string(st.id)); SaveClient(); }
        ImGui::EndCombo();
    }
    ImGui::PopFont();
}

void App::Impl::PagePractice()
{
    PageHeader("PRACTICE", "Straight into the game's practice mode with the characters you pick here.");
    BeginCard("practice");
    PracticeRows(440);
    Gap(12);
    if (BigButton(gameProc ? "GAME RUNNING" : "START PRACTICE", { 300, 54 }, installed && !gameRunning)) StartPractice();
    if (!installed) { ImGui::SameLine(0, 14); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 16); Small(col::amber, "Set up the game first (SETUP)."); }
    if (!launchNote.empty()) Small(col::amber, launchNote.c_str());
    EndCard();
    Gap();
    BeginCard("replays", 0, "Replays", "Every online fight is saved. The newest 50 are kept; star one to keep it for good.");
    ReplaysList(15);
    EndCard();
}

// ---------------------------------------------------------------- replays
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
} // namespace

void App::Impl::ReplaysList(int max)
{
    const std::string dir = ReplaysDir(), favDir = plat::JoinPath(dir, "favourites");
    static double listedAt = -100;
    static std::vector<ReplayInfo> list;
    const double now = SDL_GetTicks() / 1000.0;
    if (now - listedAt > 3) {   // new fights land while the client runs
        listedAt = now;
        list.clear();
        std::vector<std::string> recent;
        for (const std::string& f : plat::ListFilesRecursive(dir)) {
            if (f.size() < 4 || f.substr(f.size() - 4) != ".ttr") continue;
            const bool fav = f.find("favourites") != std::string::npos;
            if (!fav) recent.push_back(f);
            list.push_back(ReadReplayInfo(f, fav));
        }
        std::sort(recent.begin(), recent.end());
        if (recent.size() > 50)
            for (size_t i = 0; i + 50 < recent.size(); ++i) plat::DeleteFileAt(recent[i]);
        std::sort(list.begin(), list.end(), [](const ReplayInfo& a, const ReplayInfo& b) { return a.fav != b.fav ? a.fav : a.date > b.date; });
        if (list.size() > 60) list.resize(60);
    }
    if (list.empty()) Small(col::dim, "Your online fights show up here after you play them.");
    int shown = 0;
    for (size_t i = 0; i < list.size() && shown < max; ++i, ++shown) {
        const ReplayInfo& r = list[i];
        ImGui::PushID(int(i));
        const std::string who = (r.p1.empty() ? std::string("Player 1") : r.p1) + "  vs  " + (r.p2.empty() ? std::string("Player 2") : r.p2);
        ImGui::AlignTextToFramePadding();
        Text(F.bold, 14.f, r.fav ? col::accent : col::text, (std::string(r.fav ? "* " : "") + who).c_str());
        ImGui::SameLine(0, 12);
        Text(F.body, 13.f, col::dim, r.date.c_str());
        ImGui::SameLine(ImGui::GetContentRegionAvail().x + ImGui::GetCursorPosX() - 290);
        if (Button("Watch", { 90, 28 }, installed && !gameRunning)) StartReplay(r.path);
        ImGui::SameLine(0, 8);
        if (Button(r.fav ? "Unstar" : "Star", { 90, 28 })) {
            const size_t slash = r.path.find_last_of("/\\");
            plat::MakeDir(favDir);
            plat::MoveFileReplace(r.path, plat::JoinPath(r.fav ? dir : favDir, r.path.substr(slash + 1)));
            listedAt = -100;
        }
        ImGui::SameLine(0, 8);
        if (Button("Delete", { 90, 28 })) { plat::DeleteFileAt(r.path); listedAt = -100; }
        ImGui::PopID();
    }
    if (list.size() > size_t(shown)) Small(col::dim, ("and " + std::to_string(list.size() - size_t(shown)) + " more").c_str());
    Gap(4);
    if (Button("Open the replays folder", { 210, 30 })) { plat::MakeDir(dir); plat::OpenFolder(dir); }
}
