// TTT2 Client - ONLINE: your account, your team, the queue, a found match and its result, the leaderboard.
#include "app_impl.h"

#include <cstdio>
#include <cstring>

using namespace ui;

// A character list (`key` in client.ini's `section`); true when it changed.
bool App::Impl::CharacterCombo(const char* id, const char* section, const char* key, bool allowRandom, float width)
{
    const int cur = client.GetInt(section, key, roster::kRandom);
    bool changed = false;
    ImGui::PushID(id);
    ImGui::SetNextItemWidth(width);
    ImGui::PushFont(F.bold, 14.f);
    if (ImGui::BeginCombo("##c", cur == roster::kRandom ? "Random" : roster::CharacterName(cur), ImGuiComboFlags_HeightLarge)) {
        if (allowRandom && ImGui::Selectable("Random", cur == roster::kRandom)) { client.Set(section, key, std::to_string(roster::kRandom)); changed = true; }
        for (const roster::Character& c : roster::kCharacters)
            if (ImGui::Selectable(c.name, c.id == cur)) { client.Set(section, key, std::to_string(c.id)); changed = true; }
        ImGui::EndCombo();
    }
    ImGui::PopFont();
    ImGui::PopID();
    if (changed) SaveClient();
    return changed;
}

// Your team (online matches and practice use the same one): tag or solo, and who.
void App::Impl::TeamRows(float ctl)
{
    static const char* teams[] = { "Tag (2 characters)", "Solo (1 character)" };
    int t = client.Get("picks", "team", "duo") == "solo" ? 1 : 0;
    RowLabel("Team", "Random picks for you when the fight starts.", ctl);
    if (Segmented("team", &t, teams, 2, ctl)) { client.Set("picks", "team", t ? "solo" : "duo"); SaveClient(); }
    Gap(4);
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ctl);
    const float w = t ? ctl : (ctl - 10) * 0.5f;
    CharacterCombo("c1", "picks", "c1", true, w);
    if (!t) { ImGui::SameLine(0, 10); CharacterCombo("c2", "picks", "c2", true, w); }
    if (ranksKnown) {
        auto rank = [&](int c) { return std::string(c == roster::kRandom || c < 0 || c >= 64 || gameRank[c] < 0 ? "-" : roster::RankName(gameRank[c])); };
        const int c1 = client.GetInt("picks", "c1", 14), c2 = client.GetInt("picks", "c2", 42);
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + ImGui::GetContentRegionAvail().x - ctl);
        const std::string r = t ? "Rank: " + rank(c1) : "Ranks: " + rank(c1) + "  /  " + rank(c2);
        Text(F.bold, 13.f, col::accent, r.c_str());
    }
}

void App::Impl::PageOnline()
{
    PageHeader("ONLINE", "Ranked and casual matches on the TTT2 server. Rollback netcode, straight into the fight.");
    if (!online) StartOnline();
    const Online::View v = online->Snapshot();
    const double now = SDL_GetTicks() / 1000.0;
    char t[240];

    if (!v.notice.empty()) { Small(col::amber, v.notice.c_str()); Gap(6); }

    if (v.state == Online::NEED_NAME) {
        BeginCard("name", 0, "Pick your fighter name", "Your name on the server and the leaderboard. Your account is kept on this PC: no email, no password.");
        if (!v.error.empty()) { Text(F.bold, 14.f, col::red, v.error.c_str()); Gap(4); }
        ImGui::SetNextItemWidth(320);
        ImGui::PushFont(F.bold, 16.f);
        const bool enter = ImGui::InputTextWithHint("##name", "3 to 16 letters, digits, _ or -", nameBuf, sizeof nameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::PopFont();
        ImGui::SameLine(0, 12);
        const bool okLen = std::strlen(nameBuf) >= 3;
        if (BigButton("CREATE", { 160, 40 }, okLen) || (enter && okLen)) online->Register(nameBuf);
        EndCard();
        return;
    }
    if (v.state == Online::CONNECTING || v.state == Online::OFFLINE || v.state == Online::FAILED) {
        BeginCard("conn", 0, v.state == Online::FAILED ? "Can't reach the server" : "Connecting");
        Small(v.state == Online::FAILED ? col::red : col::dim, v.state == Online::FAILED ? (v.error + " Trying again shortly.").c_str() : "Connecting to the TTT2 server...");
        EndCard();
        return;
    }
    if (now - lastBoard > 60 && v.state == Online::LOBBY) { online->RequestBoard(); lastBoard = now; }

    // ---- you
    BeginCard("me");
    if (!renaming) {
        Text(F.heavy, 34.f, col::text, v.name.c_str());
        ImGui::SameLine(0, 14);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 9);
        if (LinkButton("Change name")) { renaming = true; std::snprintf(renameBuf, sizeof renameBuf, "%s", v.name.c_str()); }
    } else {
        ImGui::SetNextItemWidth(240);
        const bool enter = ImGui::InputText("##rename", renameBuf, sizeof renameBuf, ImGuiInputTextFlags_EnterReturnsTrue);
        ImGui::SameLine(0, 8);
        if (Button("Save", { 80, 32 }, std::strlen(renameBuf) >= 3) || (enter && std::strlen(renameBuf) >= 3)) { online->Rename(renameBuf); renaming = false; }
        ImGui::SameLine(0, 8);
        if (Button("Cancel", { 80, 32 })) renaming = false;
        Small(col::faint, "3-16 letters, digits, - or _. Your rating and record stay.");
    }
    if (!v.error.empty() && v.state == Online::LOBBY) Text(F.body, 13.f, col::red, v.error.c_str());
    std::snprintf(t, sizeof t, "Rating %d   ·   %d wins, %d losses   ·   %d online, %d searching, %d in matches", v.rating, v.wins, v.games - v.wins,
                  v.online, v.queued, v.playing);
    Text(F.bold, 14.5f, col::accent, t);
    if (!v.lastOutcome.empty()) {
        std::string line = !lastSet.empty() && lastSetFor == v.lastMatch ? lastSet : "Last match";
        if (v.lastOutcome == "disputed") line += ": the two games reported different winners, so it was not counted.";
        else if (v.lastOutcome == "abandoned") line += ": not counted.";
        else {
            if (v.lastOutcome == "forfeit") line += " (your opponent left)";
            if (v.lastChange != 0) { std::snprintf(t, sizeof t, "   ·   rating %+d", v.lastChange); line += t; }
        }
        Small(v.lastOutcome == "disputed" ? col::amber : v.lastChange > 0 ? col::green : v.lastChange < 0 ? col::red : col::dim, line.c_str());
        for (const RankChange& r : lastRanks) {
            std::string rl = roster::CharacterName(r.character);
            if (r.after > r.before) rl += std::string(": promoted to ") + roster::RankName(r.after);
            else if (r.after < r.before) rl += std::string(": demoted to ") + roster::RankName(r.after);
            else { char pts[48]; std::snprintf(pts, sizeof pts, " (%+d points)", r.pointsAfter - r.pointsBefore); rl += std::string(": stays ") + roster::RankName(r.after) + pts; }
            Small(r.after > r.before ? col::accent : r.after < r.before ? col::red : col::dim, rl.c_str());
        }
    }
    EndCard();
    Gap();

    if (v.state == Online::LOBBY) {
        BeginCard("queue", 0, "Find a match");
        const float ctl = 440;
        TeamRows(ctl);
        Divider();
        static const char* modes[] = { "Ranked", "Casual" };
        static const char* sides[] = { "Either side", "1P (left)", "2P (right)" };
        static const char* pings[] = { "60 ms", "90 ms", "120 ms", "150 ms", "200 ms" };
        RowLabel("Mode", "Ranked changes your rating and your characters' ranks. Casual doesn't.", ctl);
        if (Segmented("mode", &qMode, modes, 2, ctl)) SaveQueuePrefs();
        Divider();
        RowLabel("Side", "A locked side is guaranteed: you are never matched with someone who locked the same one.", ctl);
        if (Segmented("side", &qSide, sides, 3, ctl)) SaveQueuePrefs();
        Divider();
        RowLabel("Max ping", "Only opponents within this. Lower: better connections, longer waits.", ctl);
        if (Segmented("ping", &qPing, pings, 5, ctl)) SaveQueuePrefs();
        Gap(10);
        if (BigButton("FIND MATCH", { 280, 54 }, installed)) {
            static const char* m[] = { "ranked", "casual" };
            static const char* sd[] = { "any", "p1", "p2" };
            online->Queue(m[qMode], sd[qSide], kPings[qPing]);
        }
        if (!installed) { ImGui::SameLine(0, 14); ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 16); Small(col::amber, "Set up the game first (SETUP)."); }
        EndCard();
    } else if (v.state == Online::QUEUED) {
        BeginCard("searching");
        const int secs = int((net::NowMs() - v.queueSinceMs) / 1000);
        std::snprintf(t, sizeof t, "SEARCHING%s", std::string(size_t(int(now * 2) % 4), '.').c_str());
        Text(F.heavy, 40.f, col::text, t);
        const char* side = v.queueSide == "p1" ? "1P side" : v.queueSide == "p2" ? "2P side" : "either side";
        std::snprintf(t, sizeof t, "%s  ·  %s  ·  %d:%02d", v.queueMode == "ranked" ? "Ranked" : "Casual", side, secs / 60, secs % 60);
        Text(F.bold, 15.f, col::accent, t);
        Gap(10);
        if (Button("Cancel", { 140, 36 })) online->Cancel();
        ImGui::SameLine(0, 10);
        if (Button(gameProc ? "Practice running" : "Practice while you wait", { 220, 36 }, installed && !gameRunning)) StartPractice();
        Small(col::faint, "When a match is found you're asked to accept it, in the game too if you're practising; the match then takes over.");
        EndCard();
    } else if (v.state == Online::CHECKING || v.state == Online::READY) {
        BeginCard("found");
        Text(F.heavy, 40.f, col::text, v.state == Online::READY ? "MATCH READY" : "MATCH FOUND");
        std::snprintf(t, sizeof t, "vs %s (%d)   ·   %s   ·   you are %s", v.match.opponent.c_str(), v.match.opponentRating,
                      v.match.mode == "ranked" ? "Ranked" : "Casual", v.match.side == "p1" ? "1P (left)" : "2P (right)");
        Text(F.bold, 16.f, col::accent, t);
        Gap(2);
        ConnectionBadge(v.match.opponentConn, v.state == Online::READY ? v.match.rtt : 0);
        ImGui::SameLine(0, 8);
        ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 2);
        {
            const char* conn = v.match.opponentConn == 1 ? "Wired" : v.match.opponentConn == 2 ? "Wi-Fi" : "Connection unknown";
            std::snprintf(t, sizeof t, "%s   ·   %s", v.match.opponentRegion.empty() ? "Region unknown" : v.match.opponentRegion.c_str(), conn);
            Text(F.body, 14.f, col::dim, t);
        }
        Gap(4);
        if (v.state == Online::CHECKING) {
            const int64_t leftMs = int64_t(v.acceptDeadlineMs) - int64_t(net::NowMs());
            const float frac = std::clamp(float(leftMs) / float(Online::kAcceptMs), 0.f, 1.f);
            ImGui::PushStyleColor(ImGuiCol_PlotHistogram, V(frac < 0.3f ? col::red : col::accent));
            ImGui::PushStyleColor(ImGuiCol_FrameBg, V(col::cardHi));
            ImGui::ProgressBar(frac, { -1, 6 }, "");
            ImGui::PopStyleColor(2);
            Gap(6);
            if (!v.accepted) {
                const bool pad = !devices.empty();
                if (BigButton(pad ? "ACCEPT   (A / Cross)" : "ACCEPT   (Enter)", { 320, 52 }) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceDown, false) ||
                    ImGui::IsKeyPressed(ImGuiKey_Enter, false) || ImGui::IsKeyPressed(ImGuiKey_KeypadEnter, false))
                    online->Accept();
                ImGui::SameLine(0, 12);
                ImGui::SetCursorPosY(ImGui::GetCursorPosY() + 9);
                if (Button(pad ? "Decline   (B / Circle)" : "Decline   (Esc)", { 200, 36 }) || ImGui::IsKeyPressed(ImGuiKey_GamepadFaceRight, false) ||
                    ImGui::IsKeyPressed(ImGuiKey_Escape, false))
                    online->Decline();
                std::snprintf(t, sizeof t, "%d s to accept. Declining or letting it run out takes you out of the queue.", int(std::max<int64_t>(0, (leftMs + 999) / 1000)));
                Small(col::faint, t);
            } else {
                std::snprintf(t, sizeof t, "Accepted. Waiting for %s...", v.match.opponent.c_str());
                Text(F.bold, 15.f, col::green, t);
            }
        } else {
            std::snprintf(t, sizeof t, "Connected: %d ms, %s", v.match.rtt, v.match.direct ? "direct" : "through the server's relay");
            Text(F.body, 14.f, col::green, t);
            Gap(8);
            if (matchProc) {
                Text(F.bold, 15.f, col::text, "The game is starting.");
                std::snprintf(t, sizeof t, "It goes straight into the fight - keep your hands off the controller until it loads. You play %s on the %s side, "
                              "first to 2 rounds. The game closes by itself when the match is over, and the result comes from the game.",
                              matchTeam.c_str(), v.match.side == "p1" ? "left" : "right");
                Small(col::dim, t);
                if (setScore.Games() > 0) {
                    std::snprintf(t, sizeof t, "Score so far: you %d - %d %s", setScore.won, setScore.lost, v.match.opponent.c_str());
                    Gap(2);
                    Text(F.bold, 15.f, col::accent, t);
                }
            } else if (noResult) {
                if (setScore.Games() == 0) Small(col::amber, "The game closed without a finished match, so nothing was counted.");
                else { std::snprintf(t, sizeof t, "The set ended level (%d - %d), so nothing was counted.", setScore.won, setScore.lost); Small(col::amber, t); }
                Gap(4);
                if (Button("Start the game again", { 200, 34 })) { noResult = false; matchStarted.clear(); }
            } else if (!matchNote.empty()) {
                Small(col::amber, matchNote.c_str());
                Gap(4);
                if (Button("Try again", { 130, 34 })) { matchNote.clear(); matchStarted.clear(); }
            } else if (resultSent) {
                std::snprintf(t, sizeof t, "You %s %d - %d.", setScore.won > setScore.lost ? "won" : "lost", setScore.won, setScore.lost);
                Text(F.bold, 16.f, setScore.won > setScore.lost ? col::green : col::text, t);
                std::snprintf(t, sizeof t, "Sent to the server. It counts once %s's game has closed too.", v.match.opponent.c_str());
                Small(col::dim, t);
            } else {
                Small(col::dim, "Starting the game...");
            }
        }
        Gap(10);
        if (!matchProc && Button(v.state == Online::READY ? "Back to the lobby" : "Leave", { 170, 34 })) { online->Leave(); resultSent = noResult = false; }
        EndCard();
    }

    // ---- the leaderboard
    Gap();
    BeginCard("board", 0, "Leaderboard");
    if (v.board.empty()) {
        Small(col::faint, "Players show up here after 5 ranked matches.");
    } else if (ImGui::BeginTable("lb", 4, ImGuiTableFlags_RowBg)) {
        ImGui::TableSetupColumn("#", ImGuiTableColumnFlags_WidthFixed, 44);
        ImGui::TableSetupColumn("name", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("rating", ImGuiTableColumnFlags_WidthFixed, 80);
        ImGui::TableSetupColumn("record", ImGuiTableColumnFlags_WidthFixed, 110);
        ImGui::PushStyleColor(ImGuiCol_TableRowBg, V(0));
        ImGui::PushStyleColor(ImGuiCol_TableRowBgAlt, V(Alpha(col::text, 0.025f)));
        for (size_t i = 0; i < v.board.size() && i < 20; ++i) {
            const BoardRow& r = v.board[i];
            const bool me = r.name == v.name;
            ImGui::TableNextRow(0, 28);
            ImGui::TableNextColumn();
            std::snprintf(t, sizeof t, "%d", r.rank);
            Text(F.heavy, 18.f, r.rank <= 3 ? col::accent : col::dim, t);
            ImGui::TableNextColumn();
            Text(F.bold, 14.f, me ? col::accent : col::text, r.name.c_str());
            ImGui::TableNextColumn();
            std::snprintf(t, sizeof t, "%d", r.rating);
            Text(F.bold, 14.f, col::text, t);
            ImGui::TableNextColumn();
            std::snprintf(t, sizeof t, "%d-%d", r.wins, r.games - r.wins);
            Text(F.body, 13.f, col::dim, t);
        }
        ImGui::PopStyleColor(2);
        ImGui::EndTable();
    }
    EndCard();

    // ---- problems
    Gap();
    BeginCard("help", 0, "Something wrong?");
    Small(col::dim, "The log of your last 5 matches is kept in one file: send matches.txt to whoever is helping you.");
    Gap(6);
    if (Button("Open the logs folder", { 200, 32 })) { plat::MakeDir(TestLogsDir()); plat::OpenFolder(TestLogsDir()); }
    if (!plat::SecondPlayer()) {
        ImGui::SameLine(0, 10);
        if (Button("Test alone: open player 2", { 230, 32 })) plat::Relaunch("--player2");
        Small(col::faint, "Player 2 is a second client on this PC with its own account: search in both to play yourself.");
    } else {
        Small(col::accent, "This window is PLAYER 2 of a test on one PC: its own account and settings.");
    }
    EndCard();
}
