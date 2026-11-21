// TTT2 Client - online play against ttt2d (server/PROTOCOL.md): the account, the queues, and the connection check
// that decides whether two PCs get their match: both punch through to each other over UDP, the relay is tried at
// the same time, and the measured round trip (direct preferred) goes back to the server.
//
// Everything network runs on one thread of its own; the UI reads a snapshot (View) and sends commands.
#pragma once
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct ServerConfig {
    std::string host = "168.119.229.44";
    int port = 443;
    std::string fingerprint = "05:18:59:27:62:F4:62:BF:09:02:38:28:B8:EF:BD:12:BC:B8:EB:9D:03:2F:26:FC:46:98:CC:45:49:BD:9E:0F";
};

struct BoardRow { int rank = 0; std::string name; int rating = 0, games = 0, wins = 0; };

// What the game needs to play the match (handed over when it starts).
struct MatchInfo {
    std::string id, mode, side;   // side: p1 / p2 (this PC)
    std::string opponent;
    int opponentRating = 0;
    std::string peerAddr;          // the opponent's public UDP address
    std::string relayHost;
    int relayPort = 0;
    std::string relayToken;        // ours
    bool direct = false;
    int rtt = 0;
    uint32_t seed = 0;
};

class Online {
public:
    enum State { OFFLINE, CONNECTING, NEED_NAME, LOBBY, QUEUED, CHECKING, READY, FAILED };

    struct View {
        State state = OFFLINE;
        std::string error, notice;
        std::string name;
        int rating = 0, games = 0, wins = 0;
        int online = 0, queued = 0, playing = 0;
        std::string queueMode, queueSide;
        uint64_t queueSinceMs = 0;
        MatchInfo match;
        std::string checkStage;     // what the connection check is doing
        int lastChange = 0;         // the last rated match's change
        std::string lastOutcome;
        std::vector<BoardRow> board;
    };

    explicit Online(ServerConfig cfg, std::string dataDir);
    ~Online();

    void Start();                       // connect (and stay connected; reconnects by itself)
    void Stop();
    View Snapshot() const;

    void Register(const std::string& name);
    void Queue(const std::string& mode, const std::string& side, int maxPing);
    void Cancel();
    void Leave();                       // back to the lobby from a found match
    void RequestBoard();
    void Report(bool iWon);             // the match's result (from the game)

    std::string clientVersion;
    bool testNoDirect = false;   // tests: act as if the opponent cannot be reached directly (relay only)

private:
    void Run();
    void Post(std::function<void()> f);
    void Handle(const std::string& text);
    void OnUdp(const std::string& from, const std::string& data);
    void RunCheck(uint64_t now);
    void SendJson(const std::string& text);
    std::string LoadToken();
    void SaveToken(const std::string& token);

    ServerConfig cfg_;
    std::string dataDir_;
    mutable std::mutex m_;
    View view_;
    std::deque<std::function<void()>> commands_;
    std::thread thread_;
    std::atomic<bool> stop_{ false };

    // owned by the network thread
    struct Impl;
    Impl* p_ = nullptr;
};
