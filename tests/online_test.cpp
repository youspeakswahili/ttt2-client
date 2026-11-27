// End to end: a local ttt2d and two clients (the real Online code over TLS + UDP) find each other, check their
// connection (direct, then relay only), get the match, report a result, and see their ratings change.
#include "../client/online.h"
#include "../client/platform.h"
#include <cassert>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <thread>

static const char* Name(Online::State s)
{
    static const char* n[] = { "OFFLINE", "CONNECTING", "NEED_NAME", "LOBBY", "QUEUED", "CHECKING", "READY", "FAILED" };
    return n[s];
}
static bool WaitFor(Online& o, Online::State want, int ms, const char* who)
{
    for (int i = 0; i < ms / 20; ++i) {
        if (o.Snapshot().state == want) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    auto v = o.Snapshot();
    std::printf("%s: wanted %s, is %s (%s)\n", who, Name(want), Name(v.state), v.error.c_str());
    std::fflush(stdout);
    return false;
}

int main(int argc, char** argv)
{
    setvbuf(stdout, nullptr, _IONBF, 0);
    ServerConfig cfg;
    cfg.host = "127.0.0.1";
    cfg.port = std::atoi(argv[1]);
    cfg.fingerprint = argv[2];
    const bool relayOnly = argc > 3 && std::string(argv[3]) == "relay";
    const std::string base = std::string("/tmp/online_test_") + (relayOnly ? "relay" : "direct");
    mkdir(base.c_str(), 0700);
    mkdir((base + "/a").c_str(), 0700);
    mkdir((base + "/b").c_str(), 0700);
    Online a(cfg, base + "/a"), b(cfg, base + "/b");
    a.clientVersion = b.clientVersion = "0.2.0";
    a.testNoDirect = b.testNoDirect = relayOnly;
    a.Start();
    b.Start();
    assert(WaitFor(a, Online::NEED_NAME, 3000, "a"));
    assert(WaitFor(b, Online::NEED_NAME, 3000, "b"));
    a.Register(relayOnly ? "RelayA" : "Mumbai");
    b.Register(relayOnly ? "RelayB" : "Sparring");
    assert(WaitFor(a, Online::LOBBY, 5000, "a"));
    assert(WaitFor(b, Online::LOBBY, 5000, "b"));
    std::printf("signed in: %s %d, %s %d\n", a.Snapshot().name.c_str(), a.Snapshot().rating, b.Snapshot().name.c_str(), b.Snapshot().rating);
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));   // both reflect their UDP address

    a.Queue("ranked", "p1", 150);
    b.Queue("ranked", "any", 150);
    assert(WaitFor(a, Online::CHECKING, 5000, "a"));
    assert(WaitFor(b, Online::CHECKING, 5000, "b"));
    a.Accept();                                                      // a found match waits for both players to take it
    b.Accept();
    assert(WaitFor(a, Online::READY, 8000, "a"));
    assert(WaitFor(b, Online::READY, 8000, "b"));
    auto va = a.Snapshot(), vb = b.Snapshot();
    std::printf("match %s: %s is %s vs %s, rtt %d ms, %s\n", va.match.id.c_str(), va.name.c_str(), va.match.side.c_str(),
                va.match.opponent.c_str(), va.match.rtt, va.match.direct ? "direct" : "relay");
    assert(va.match.side == "p1" && vb.match.side == "p2");          // a locked 1P
    assert(va.match.direct == !relayOnly);
    assert(va.match.seed == vb.match.seed && va.match.seed != 0);

    // The game's transport (net_ttt2.cpp, built for Linux as NT_BIN) takes over each client's UDP port and plays a
    // stream of messages through what the server handed out: the opponent's address, the relay and its tokens.
    if (const char* nt = std::getenv("NT_BIN")) {
        const int pa = a.HandOffUdp(), pb = b.HandOffUdp();
        assert(pa > 0 && pb > 0);
        auto ini = [&](const MatchInfo& m, int port, const std::string& file) {
            const std::string text = "id=" + m.id + "\nslot=" + (m.side == "p1" ? "0" : "1") + "\npeer=" + m.peerAddr + "\nrelay=" +
                                     m.relayAddr + "\ntoken=" + m.relayToken + "\nlocalport=" + std::to_string(port) +
                                     "\ndirect=" + (m.direct ? "1" : "0") + "\n";
            FILE* f = std::fopen(file.c_str(), "w");
            std::fputs(text.c_str(), f);
            std::fclose(f);
            std::printf("%s", text.c_str());
        };
        ini(va.match, pa, base + "/a.ini");
        ini(vb.match, pb, base + "/b.ini");
        const std::string cmd = "TTT2_MATCH=" + base + "/a.ini " + nt + " " + (std::getenv("NT_N") ? std::getenv("NT_N") : "1500") + " > " + base + "/a.out 2>&1 & A=$!; TTT2_MATCH=" + base +
                                "/b.ini " + nt + " " + (std::getenv("NT_N") ? std::getenv("NT_N") : "1500") + " > " + base + "/b.out 2>&1; B=$?; wait $A; A=$?; cat " + base + "/a.out " + base +
                                "/b.out | grep -v '^\\[ttt2net\\] sending'; test $A = 0 -a $B = 0";
        const int r = std::system(cmd.c_str());
        assert(r == 0);
        a.ReclaimUdp();
        b.ReclaimUdp();
        std::puts("the game transport played through the match the server made");
    }

    const std::string matchId = va.match.id;
    a.Report(true);
    b.Report(false);
    // a new name (taken: the opponent's), then a free one
    {
        const std::string other = vb.name;
        a.Rename(other);
        for (int i = 0; i < 100 && a.Snapshot().error.empty(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        assert(!a.Snapshot().error.empty());
        a.Rename("Renamed_" + std::to_string(std::rand() % 1000));
        for (int i = 0; i < 100 && a.Snapshot().name.rfind("Renamed_", 0) != 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(20));
        assert(a.Snapshot().name.rfind("Renamed_", 0) == 0);
        std::printf("renamed to %s\n", a.Snapshot().name.c_str());
    }
    assert(WaitFor(a, Online::LOBBY, 5000, "a"));
    assert(WaitFor(b, Online::LOBBY, 5000, "b"));
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    va = a.Snapshot();
    vb = b.Snapshot();
    std::printf("rated: %s %d (%+d), %s %d (%+d)\n", va.name.c_str(), va.rating, va.lastChange, vb.name.c_str(), vb.rating, vb.lastChange);
    assert(va.rating == 1520 && vb.rating == 1480 && va.lastOutcome == "agreed");
    assert(va.lastMatch == matchId && vb.lastMatch == matchId);      // which match the outcome belongs to

    // signing in again uses the saved account
    a.Stop();
    Online again(cfg, base + "/a");
    again.clientVersion = "0.2.0";
    again.Start();
    assert(WaitFor(again, Online::LOBBY, 5000, "again"));
    assert(again.Snapshot().rating == 1520);
    std::printf("signed in again from the saved account: %s %d\n", again.Snapshot().name.c_str(), again.Snapshot().rating);
    std::puts(relayOnly ? "online (relay only): all checks passed" : "online (direct): all checks passed");
    std::fflush(stdout);
    std::_Exit(0);
}
