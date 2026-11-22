#include "online.h"
#include "json.h"
#include "net.h"
#include "platform.h"

#include <algorithm>
#include <cstring>
#include <map>

namespace {
constexpr uint8_t kReflect = 0x01, kReflected = 0x02, kRelay = 0x03, kRelayed = 0x04;
constexpr uint8_t kPing = 0x20, kPong = 0x21;
constexpr uint64_t kCheckMs = 3500;    // how long the connection check measures
constexpr int kSamplesWanted = 8;

bool HexTo(const std::string& h, uint8_t* out, size_t n)
{
    if (h.size() != n * 2) return false;
    for (size_t i = 0; i < n; ++i) {
        const auto nib = [](char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1; };
        const int a = nib(h[2 * i]), b = nib(h[2 * i + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = uint8_t(a * 16 + b);
    }
    return true;
}
int Median(std::vector<int> v)
{
    if (v.empty()) return 0;
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}
} // namespace

struct Online::Impl {
    std::unique_ptr<net::Ws> ws;
    std::unique_ptr<net::Udp> udp;
    std::string udpToken;        // reflect token from welcome
    int udpPort = 27620;
    uint64_t nextReflect = 0, nextPing = 0, nextConnect = 0;
    std::string pendingRegister;  // a name to register on the next connection
    bool renaming = false;        // a rename was asked: its error is only a message
    int handedPort = 0;           // the port the game has now

    // the connection check of the current proposal
    bool checking = false, checkSent = false;
    uint64_t checkStart = 0, nextProbe = 0;
    uint8_t matchId[8] = {};
    uint8_t mySlot = 0;
    std::string peerAddr, relayToken, serverUdp;
    uint32_t seq = 0;
    std::map<uint32_t, std::pair<uint64_t, int>> sent;   // seq -> (time, path 0 direct / 1 relay)
    std::vector<int> directRtt, relayRtt;
};

Online::Online(ServerConfig cfg, std::string dataDir) : cfg_(std::move(cfg)), dataDir_(std::move(dataDir)) {}
Online::~Online() { Stop(); }

void Online::Start()
{
    if (thread_.joinable()) return;
    stop_ = false;
    thread_ = std::thread([this] { Run(); });
}
void Online::Stop()
{
    stop_ = true;
    if (thread_.joinable()) thread_.join();
}
Online::View Online::Snapshot() const
{
    std::lock_guard l(m_);
    return view_;
}
void Online::Post(std::function<void()> f)
{
    std::lock_guard l(m_);
    commands_.push_back(std::move(f));
}

std::string Online::LoadToken()
{
    std::string blob;
    if (!plat::ReadBinary(plat::JoinPath(dataDir_, "account.dat"), blob) || blob.empty()) return {};
    return net::Unprotect(blob);
}
void Online::SaveToken(const std::string& token)
{
    const std::string blob = net::Protect(token);
    if (!blob.empty()) plat::WriteBinary(plat::JoinPath(dataDir_, "account.dat"), blob);
}

void Online::SendJson(const std::string& text)
{
    if (p_ && p_->ws) p_->ws->Send(text);
}

void Online::Register(const std::string& name)
{
    Post([this, name] {
        p_->pendingRegister = name;
        {
            std::lock_guard l(m_);
            view_.state = CONNECTING;
            view_.error.clear();
        }
        if (p_->ws) { p_->ws->Close(); p_->ws.reset(); }
        p_->nextConnect = 0;
    });
}
void Online::Rename(const std::string& name)
{
    Post([this, name] {
        p_->renaming = true;
        SendJson("{\"t\":\"rename\",\"name\":" + json::Quote(name) + "}");
    });
}
void Online::Queue(const std::string& mode, const std::string& side, int maxPing)
{
    Post([=] {
        SendJson("{\"t\":\"queue\",\"mode\":" + json::Quote(mode) + ",\"side\":" + json::Quote(side) + ",\"maxPing\":" + std::to_string(maxPing) + "}");
    });
}
void Online::Cancel()
{
    Post([this] {
        SendJson("{\"t\":\"cancel\"}");
        std::lock_guard l(m_);
        if (view_.state == QUEUED) view_.state = LOBBY;
    });
}
void Online::Leave()
{
    Post([this] {
        std::string id;
        {
            std::lock_guard l(m_);
            id = view_.match.id;
            view_.state = LOBBY;
            view_.match = {};
        }
        p_->checking = false;
        if (!id.empty()) SendJson("{\"t\":\"leave\",\"match\":" + json::Quote(id) + "}");
    });
}
void Online::RequestBoard() { Post([this] { SendJson("{\"t\":\"leaderboard\"}"); }); }
void Online::Report(bool iWon)
{
    Post([this, iWon] {
        std::string id;
        {
            std::lock_guard l(m_);
            id = view_.match.id;
        }
        if (!id.empty()) SendJson("{\"t\":\"result\",\"match\":" + json::Quote(id) + ",\"winner\":\"" + (iWon ? "me" : "opponent") + "\"}");
    });
}

int Online::HandOffUdp()
{
    handedPort_ = -1;
    Post([this] {
        Impl& impl = *p_;
        const int port = impl.udp ? impl.udp->LocalPort() : 0;
        impl.udp.reset();
        impl.handedPort = port;
        handedPort_ = port;
    });
    for (int i = 0; i < 100 && handedPort_ < 0; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    return handedPort_ < 0 ? 0 : handedPort_.load();
}

void Online::ReclaimUdp()
{
    Post([this] {
        Impl& impl = *p_;
        if (impl.udp) return;
        std::string err;
        impl.udp = net::UdpOpen(err, impl.handedPort);
        if (!impl.udp) impl.udp = net::UdpOpen(err);
        impl.handedPort = 0;
        impl.nextReflect = 0;
    });
}

void Online::Run()
{
    Impl impl;
    p_ = &impl;
    std::string err;
    impl.udp = net::UdpOpen(err);
    uint64_t backoff = 2000;
    while (!stop_) {
        const uint64_t now = net::NowMs();
        // commands from the UI
        std::deque<std::function<void()>> cmds;
        {
            std::lock_guard l(m_);
            cmds.swap(commands_);
        }
        for (auto& c : cmds) c();

        // connect (and reconnect)
        if (!impl.ws && now >= impl.nextConnect) {
            const std::string token = LoadToken();
            if (token.empty() && impl.pendingRegister.empty()) {
                std::lock_guard l(m_);
                view_.state = NEED_NAME;
            } else {
                {
                    std::lock_guard l(m_);
                    view_.state = CONNECTING;
                }
                impl.ws = net::WsConnect(cfg_.host, cfg_.port, "/v1", cfg_.fingerprint, err);
                if (!impl.ws) {
                    std::lock_guard l(m_);
                    view_.state = FAILED;
                    view_.error = err;
                    impl.nextConnect = now + backoff;
                    backoff = std::min<uint64_t>(backoff * 2, 30000);
                } else {
                    backoff = 2000;
                    if (!impl.pendingRegister.empty())
                        SendJson("{\"t\":\"register\",\"v\":1,\"name\":" + json::Quote(impl.pendingRegister) + ",\"client\":" + json::Quote(clientVersion) + "}");
                    else
                        SendJson("{\"t\":\"hello\",\"v\":1,\"token\":" + json::Quote(token) + ",\"client\":" + json::Quote(clientVersion) + "}");
                    impl.nextPing = now + 20000;
                }
            }
        }

        // the server's messages
        if (impl.ws) {
            std::string msg;
            int r;
            while ((r = impl.ws->Recv(msg, 0)) == 1) Handle(msg);
            if (r < 0) {
                {
                    std::lock_guard l(m_);
                    if (view_.state != NEED_NAME) { view_.state = FAILED; if (view_.error.empty()) view_.error = impl.ws->Error(); }
                }
                impl.ws.reset();
                impl.checking = false;
                impl.nextConnect = now + backoff;
            } else if (now >= impl.nextPing) {
                SendJson("{\"t\":\"ping\"}");
                impl.nextPing = now + 20000;
            }
        }

        // UDP: tell the server our public address every 2 s while signed in; the check's probes; answers
        if (impl.udp) {
            if (impl.ws && !impl.udpToken.empty() && now >= impl.nextReflect) {
                uint8_t pkt[17] = { kReflect };
                if (HexTo(impl.udpToken, pkt + 1, 16)) impl.udp->SendTo(impl.serverUdp, pkt, sizeof pkt);
                impl.nextReflect = now + 2000;
            }
            if (impl.checking) RunCheck(now);
            std::string from, data;
            const int timeout = impl.checking ? 2 : 20;
            for (int i = 0; i < 64 && impl.udp->Recv(from, data, i == 0 ? timeout : 0) == 1; ++i) OnUdp(from, data);
        } else {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
    if (impl.ws) impl.ws->Close();
    p_ = nullptr;
}

void Online::Handle(const std::string& text)
{
    json::Value m;
    if (!json::Parse(text, m)) return;
    const std::string t = m["t"].Str();
    Impl& impl = *p_;
    std::lock_guard l(m_);
    View& v = view_;
    if (t == "welcome" || t == "registered") {
        if (t == "registered") {
            m_.unlock();
            SaveToken(m["token"].Str());
            m_.lock();
            impl.pendingRegister.clear();
        }
        v.state = LOBBY;
        v.error.clear();
        v.name = m["name"].Str();
        v.rating = m["rating"].Int();
        v.games = m["games"].Int();
        v.wins = m["wins"].Int();
        impl.udpToken = m["udpToken"].Str();
        impl.udpPort = m["udpPort"].Int() ? m["udpPort"].Int() : 27620;
        impl.serverUdp = cfg_.host + ":" + std::to_string(impl.udpPort);
        impl.nextReflect = 0;
        impl.ws->Send("{\"t\":\"leaderboard\"}");
    } else if (t == "renamed") {
        impl.renaming = false;
        v.name = m["name"].Str();
        v.error.clear();
        v.notice = "You are " + v.name + " now.";
    } else if (t == "error" && impl.renaming && (m["code"].Str() == "name_taken" || m["code"].Str() == "bad_name")) {
        impl.renaming = false;
        v.error = m["message"].Str();
    } else if (t == "error") {
        const std::string code = m["code"].Str();
        v.error = m["message"].Str();
        if (code == "bad_token") {
            v.state = NEED_NAME;
            m_.unlock();
            plat::DeleteFileAt(plat::JoinPath(dataDir_, "account.dat"));
            m_.lock();
        } else if (code == "name_taken" || code == "bad_name") {
            v.state = NEED_NAME;
            impl.pendingRegister.clear();
        } else if (code == "outdated") {
            v.state = FAILED;
        }
    } else if (t == "stats") {
        v.online = m["online"].Int();
        v.queued = m["queued"].Int();
        v.playing = m["playing"].Int();
    } else if (t == "queued") {
        if (v.state != QUEUED) v.queueSinceMs = net::NowMs();
        v.state = QUEUED;
        v.queueMode = m["mode"].Str();
        v.queueSide = m["side"].Str();
    } else if (t == "proposal") {
        v.state = CHECKING;
        v.match = {};
        v.match.id = m["match"].Str();
        v.match.mode = m["mode"].Str();
        v.match.side = m["side"].Str();
        v.match.opponent = m["opponent"]["name"].Str();
        v.match.opponentRating = m["opponent"]["rating"].Int();
        v.match.peerAddr = m["peer"]["addr"].Str();
        v.match.relayHost = cfg_.host;
        v.match.relayPort = m["relay"]["port"].Int();
        v.match.relayToken = m["relay"]["token"].Str();
        v.checkStage = "Connecting to " + v.match.opponent + "...";
        impl.checking = HexTo(v.match.id, impl.matchId, 8);
        impl.checkSent = false;
        impl.checkStart = net::NowMs();
        impl.nextProbe = 0;
        impl.mySlot = v.match.side == "p1" ? 0 : 1;
        impl.peerAddr = v.match.peerAddr;
        impl.relayToken = v.match.relayToken;
        impl.sent.clear();
        impl.directRtt.clear();
        impl.relayRtt.clear();
    } else if (t == "go") {
        if (v.match.id == m["match"].Str()) {
            v.state = READY;
            v.match.relayAddr = v.match.relayPort ? v.match.relayHost + ":" + std::to_string(v.match.relayPort) : impl.serverUdp;
            v.match.localPort = impl.udp ? impl.udp->LocalPort() : 0;
            v.match.direct = m["direct"].Bool();
            v.match.rtt = m["rtt"].Int();
            v.match.seed = uint32_t(m["seed"].Num());
            v.checkStage.clear();
        }
        impl.checking = false;
    } else if (t == "cancelled") {
        const std::string why = m["reason"].Str();
        v.notice = why == "too_far" ? "That opponent was too far away. Still searching."
                 : why == "check_failed" ? "Could not connect to that opponent. Still searching."
                 : why == "opponent_left" ? "The opponent left. Still searching."
                 : "The match did not start. Still searching.";
        v.match = {};
        v.state = LOBBY;   // a "queued" follows when we are back in the queue
        impl.checking = false;
    } else if (t == "left") {
        v.notice = "Your opponent left the match.";
    } else if (t == "rated") {
        v.lastChange = m["change"].Int();
        v.lastOutcome = m["outcome"].Str();
        v.rating = m["rating"].Int();
        v.games = m["games"].Int();
        v.wins = m["wins"].Int();
        v.match = {};
        v.state = LOBBY;
    } else if (t == "leaderboard") {
        v.board.clear();
        for (const json::Value& r : m["rows"].a)
            v.board.push_back({ r["rank"].Int(), r["name"].Str(), r["rating"].Int(), r["games"].Int(), r["wins"].Int() });
    }
}

// Probe: type, match id (8), my slot, seq (4, little endian), path (0 direct, 1 relay)
void Online::RunCheck(uint64_t now)
{
    Impl& impl = *p_;
    if (!impl.checkSent && now >= impl.nextProbe && now - impl.checkStart < kCheckMs) {
        uint8_t probe[15] = { kPing };
        std::memcpy(probe + 1, impl.matchId, 8);
        probe[9] = impl.mySlot;
        for (int path = 0; path < 2; ++path) {
            const uint32_t s = ++impl.seq;
            std::memcpy(probe + 10, &s, 4);
            probe[14] = uint8_t(path);
            if (path == 0) {
                if (impl.peerAddr.empty() || testNoDirect) continue;
                impl.udp->SendTo(impl.peerAddr, probe, sizeof probe);
            } else {
                uint8_t pkt[1 + 16 + sizeof probe] = { kRelay };
                if (!HexTo(impl.relayToken, pkt + 1, 16)) continue;
                std::memcpy(pkt + 17, probe, sizeof probe);
                impl.udp->SendTo(impl.serverUdp, pkt, sizeof pkt);
            }
            impl.sent[s] = { now, path };
        }
        impl.nextProbe = now + 100;
        std::lock_guard l(m_);
        view_.checkStage = impl.directRtt.empty() && impl.relayRtt.empty() ? "Connecting to " + view_.match.opponent + "..."
                                                                         : "Measuring the connection...";
    }
    const bool enough = int(impl.directRtt.size()) >= kSamplesWanted;
    if (!impl.checkSent && (enough || now - impl.checkStart >= kCheckMs)) {
        impl.checkSent = true;
        bool ok = true, direct = true;
        int rtt = 0;
        if (impl.directRtt.size() >= 3) rtt = Median(impl.directRtt);
        else if (impl.relayRtt.size() >= 3) { rtt = Median(impl.relayRtt); direct = false; }
        else ok = false;
        std::string id;
        {
            std::lock_guard l(m_);
            id = view_.match.id;
            view_.checkStage = ok ? "Connected: " + std::to_string(rtt) + " ms " + (direct ? "(direct)" : "(through the relay)") + ". Waiting for the opponent..."
                                  : "Could not connect.";
        }
        SendJson("{\"t\":\"check\",\"match\":" + json::Quote(id) + ",\"ok\":" + (ok ? "true" : "false") + ",\"rtt\":" + std::to_string(rtt) +
                 ",\"direct\":" + (direct ? "true" : "false") + "}");
    }
}

void Online::OnUdp(const std::string& from, const std::string& data)
{
    Impl& impl = *p_;
    if (data.empty()) return;
    const auto* d = reinterpret_cast<const uint8_t*>(data.data());
    const bool fromServer = from == impl.serverUdp;
    if (fromServer && d[0] == kReflected) return;   // our public address; the server keeps it
    const uint8_t* body = d;
    size_t n = data.size();
    int path = 0;
    if (fromServer && d[0] == kRelayed) { body = d + 1; --n; path = 1; }
    else if (fromServer) return;
    if (path == 0 && testNoDirect) return;
    if (n != 15 || (body[0] != kPing && body[0] != kPong) || !impl.checking || std::memcmp(body + 1, impl.matchId, 8) != 0) return;
    if (body[9] == impl.mySlot) return;   // our own packet (a relay echo)
    if (body[0] == kPing) {
        uint8_t pong[15];
        std::memcpy(pong, body, 15);
        pong[0] = kPong;
        pong[9] = impl.mySlot;
        if (path == 0) {
            impl.udp->SendTo(from, pong, sizeof pong);
        } else {
            uint8_t pkt[1 + 16 + 15] = { kRelay };
            if (HexTo(impl.relayToken, pkt + 1, 16)) {
                std::memcpy(pkt + 17, pong, 15);
                impl.udp->SendTo(impl.serverUdp, pkt, sizeof pkt);
            }
        }
        if (path == 0 && impl.peerAddr != from) impl.peerAddr = from;   // the opponent's real address (NAT)
        return;
    }
    uint32_t s;
    std::memcpy(&s, body + 10, 4);
    auto it = impl.sent.find(s);
    if (it == impl.sent.end()) return;
    const int rtt = int(net::NowMs() - it->second.first);
    (it->second.second == 0 ? impl.directRtt : impl.relayRtt).push_back(rtt);
    impl.sent.erase(it);
    if (path == 0) {
        std::lock_guard l(m_);
        view_.match.peerAddr = from;
    }
}
