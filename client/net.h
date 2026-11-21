// TTT2 Client - networking for online play: a secure WebSocket to the matchmaking server (ttt2d) whose certificate
// is pinned (its SHA-256 fingerprint must match, so nobody can stand in for the server), and a UDP socket for the
// connection check (hole punching to the opponent, the relay). net_win.cpp: WinHTTP + Winsock. net_posix.cpp:
// OpenSSL + BSD sockets (tests and UI work off Windows).
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace net {

// One WebSocket: text messages in and out. Receiving happens on its own thread; Recv takes the next message.
class Ws {
public:
    virtual ~Ws() = default;
    virtual bool Send(const std::string& text) = 0;
    // 1: a message in `text`; 0: none within timeoutMs; -1: the connection is closed (error says why)
    virtual int Recv(std::string& text, int timeoutMs) = 0;
    virtual std::string Error() const = 0;
    virtual void Close() = 0;
};
// Blocking (a few seconds at most). fingerprint: "AB:CD:..." (SHA-256 of the server's certificate).
std::unique_ptr<Ws> WsConnect(const std::string& host, int port, const std::string& path, const std::string& fingerprint,
                              std::string& error);

// A UDP socket (IPv4), bound to any free port. Addresses are "a.b.c.d:port".
class Udp {
public:
    virtual ~Udp() = default;
    virtual bool SendTo(const std::string& addr, const void* data, size_t size) = 0;
    // 1: a packet; 0: none within timeoutMs; -1: error
    virtual int Recv(std::string& from, std::string& data, int timeoutMs) = 0;
};
std::unique_ptr<Udp> UdpOpen(std::string& error);

// Keeps the account's secret for this Windows user only (DPAPI); a no-op off Windows.
std::string Protect(const std::string& secret);
std::string Unprotect(const std::string& blob);

uint64_t NowMs();   // a steady clock

} // namespace net
