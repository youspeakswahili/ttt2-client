// TTT2 Client - net.h off Windows (tests, UI work): OpenSSL + a small WebSocket client, BSD UDP sockets.
#include "net.h"

#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <poll.h>
#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

#include <chrono>
#include <condition_variable>
#include <cstring>
#include <deque>
#include <mutex>
#include <random>
#include <strings.h>
#include <thread>

namespace net {
namespace {

std::string Hex(const unsigned char* p, size_t n)
{
    static const char* d = "0123456789ABCDEF";
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        if (i) s += ':';
        s += d[p[i] >> 4];
        s += d[p[i] & 15];
    }
    return s;
}

class PosixWs : public Ws {
public:
    int fd = -1;
    SSL_CTX* ctx = nullptr;
    SSL* ssl = nullptr;
    std::thread reader;
    std::mutex m, io;
    std::condition_variable cv;
    std::deque<std::string> inbox;
    bool closed = false;
    std::string error;

    ~PosixWs() override { Close(); }

    bool ReadN(void* p, size_t n)
    {
        auto* b = static_cast<unsigned char*>(p);
        while (n) {
            const int r = SSL_read(ssl, b, int(n));
            if (r <= 0) return false;
            b += r;
            n -= size_t(r);
        }
        return true;
    }
    void Start()
    {
        reader = std::thread([this] {
            std::string msg;
            for (;;) {
                unsigned char h[2];
                if (!ReadN(h, 2)) break;
                uint64_t n = h[1] & 0x7F;
                if (n == 126) { unsigned char e[2]; if (!ReadN(e, 2)) break; n = (uint64_t(e[0]) << 8) | e[1]; }
                else if (n == 127) { unsigned char e[8]; if (!ReadN(e, 8)) break; n = 0; for (unsigned char c : e) n = (n << 8) | c; }
                if (n > (1u << 20)) break;
                std::string payload(size_t(n), '\0');
                if (n && !ReadN(payload.data(), size_t(n))) break;
                const int op = h[0] & 0x0F;
                if (op == 0x8) break;
                if (op == 0x9) { SendFrame(0xA, payload); continue; }
                if (op == 0xA) continue;
                msg += payload;
                if (h[0] & 0x80) {
                    std::lock_guard l(m);
                    inbox.push_back(std::move(msg));
                    msg.clear();
                    cv.notify_all();
                }
            }
            std::lock_guard l(m);
            closed = true;
            if (error.empty()) error = "The connection was closed.";
            cv.notify_all();
        });
    }
    bool SendFrame(int op, const std::string& payload)
    {
        std::lock_guard l(io);
        if (!ssl) return false;
        std::string f;
        f += char(0x80 | op);
        const size_t n = payload.size();
        if (n < 126) f += char(0x80 | n);
        else if (n <= 0xFFFF) { f += char(0x80 | 126); f += char(n >> 8); f += char(n); }
        else { f += char(0x80 | 127); for (int i = 7; i >= 0; --i) f += char(uint64_t(n) >> (8 * i)); }
        static thread_local std::mt19937 rng{ std::random_device{}() };
        unsigned char mask[4];
        for (auto& b : mask) b = static_cast<unsigned char>(rng());
        f.append(reinterpret_cast<char*>(mask), 4);
        for (size_t i = 0; i < n; ++i) f += char(payload[i] ^ mask[i % 4]);
        return SSL_write(ssl, f.data(), int(f.size())) == int(f.size());
    }
    bool Send(const std::string& text) override { return SendFrame(0x1, text); }
    int Recv(std::string& text, int timeoutMs) override
    {
        std::unique_lock l(m);
        cv.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return !inbox.empty() || closed; });
        if (!inbox.empty()) { text = std::move(inbox.front()); inbox.pop_front(); return 1; }
        return closed ? -1 : 0;
    }
    std::string Error() const override { std::lock_guard l(const_cast<std::mutex&>(m)); return error; }
    void Close() override
    {
        if (fd >= 0) shutdown(fd, SHUT_RDWR);
        if (reader.joinable()) reader.join();
        std::lock_guard l(io);
        if (ssl) { SSL_free(ssl); ssl = nullptr; }
        if (ctx) { SSL_CTX_free(ctx); ctx = nullptr; }
        if (fd >= 0) { close(fd); fd = -1; }
    }
};

class PosixUdp : public Udp {
public:
    int fd = -1;
    ~PosixUdp() override { if (fd >= 0) close(fd); }
    int LocalPort() const override
    {
        sockaddr_in a{};
        socklen_t n = sizeof a;
        return getsockname(fd, reinterpret_cast<sockaddr*>(&a), &n) == 0 ? ntohs(a.sin_port) : 0;
    }
    static bool Parse(const std::string& addr, sockaddr_in& out)
    {
        const size_t colon = addr.rfind(':');
        if (colon == std::string::npos) return false;
        out = {};
        out.sin_family = AF_INET;
        out.sin_port = htons(uint16_t(std::atoi(addr.c_str() + colon + 1)));
        return inet_pton(AF_INET, addr.substr(0, colon).c_str(), &out.sin_addr) == 1 && out.sin_port != 0;
    }
    bool SendTo(const std::string& addr, const void* data, size_t size) override
    {
        sockaddr_in to;
        if (!Parse(addr, to)) return false;
        return sendto(fd, data, size, 0, reinterpret_cast<sockaddr*>(&to), sizeof to) == ssize_t(size);
    }
    int Recv(std::string& from, std::string& data, int timeoutMs) override
    {
        pollfd p{ fd, POLLIN, 0 };
        const int r = poll(&p, 1, timeoutMs);
        if (r <= 0) return r == 0 ? 0 : -1;
        char buf[2048];
        sockaddr_in a{};
        socklen_t alen = sizeof a;
        const ssize_t n = recvfrom(fd, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&a), &alen);
        if (n < 0) return 0;
        char ip[INET_ADDRSTRLEN] = {};
        inet_ntop(AF_INET, &a.sin_addr, ip, sizeof ip);
        from = std::string(ip) + ":" + std::to_string(ntohs(a.sin_port));
        data.assign(buf, size_t(n));
        return 1;
    }
};

} // namespace

std::unique_ptr<Ws> WsConnect(const std::string& host, int port, const std::string& path, const std::string& fingerprint, std::string& error)
{
    std::signal(SIGPIPE, SIG_IGN);   // a write to a closed connection is an error to handle, not the end of the process
    addrinfo hints{}, *res = nullptr;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    if (getaddrinfo(host.c_str(), std::to_string(port).c_str(), &hints, &res) != 0 || !res) { error = "Could not find the server."; return nullptr; }
    auto c = std::make_unique<PosixWs>();
    c->fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
    const bool connected = c->fd >= 0 && connect(c->fd, res->ai_addr, res->ai_addrlen) == 0;
    freeaddrinfo(res);
    if (!connected) { error = "Could not reach the server."; return nullptr; }
    c->ctx = SSL_CTX_new(TLS_client_method());
    SSL_CTX_set_verify(c->ctx, SSL_VERIFY_NONE, nullptr);   // checked by fingerprint below
    c->ssl = SSL_new(c->ctx);
    SSL_set_fd(c->ssl, c->fd);
    SSL_set_tlsext_host_name(c->ssl, host.c_str());
    if (SSL_connect(c->ssl) != 1) { error = "The secure connection failed."; return nullptr; }
    X509* cert = SSL_get1_peer_certificate(c->ssl);
    unsigned char sum[32];
    unsigned int len = 0;
    std::string fp;
    if (cert && X509_digest(cert, EVP_sha256(), sum, &len) && len == 32) fp = Hex(sum, 32);
    if (cert) X509_free(cert);
    if (fp.empty() || strcasecmp(fp.c_str(), fingerprint.c_str()) != 0) { error = "The server's identity did not match. Not connecting."; return nullptr; }
    const std::string req = "GET " + path + " HTTP/1.1\r\nHost: " + host + "\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\nSec-WebSocket-Version: 13\r\n\r\n";
    SSL_write(c->ssl, req.data(), int(req.size()));
    std::string head;
    char ch;
    while (head.size() < 4096 && head.find("\r\n\r\n") == std::string::npos) {
        if (SSL_read(c->ssl, &ch, 1) != 1) break;
        head += ch;
    }
    if (head.rfind("HTTP/1.1 101", 0) != 0) { error = "The server did not open the connection."; return nullptr; }
    c->Start();
    return c;
}

std::unique_ptr<Udp> UdpOpen(std::string& error, int port)
{
    auto u = std::make_unique<PosixUdp>();
    u->fd = socket(AF_INET, SOCK_DGRAM, 0);
    sockaddr_in any{};
    any.sin_family = AF_INET;
    any.sin_port = htons(uint16_t(port));
    if (u->fd < 0 || bind(u->fd, reinterpret_cast<sockaddr*>(&any), sizeof any) != 0) { error = "Could not open a UDP socket."; return nullptr; }
    return u;
}

std::string Protect(const std::string& s) { return s; }
std::string Unprotect(const std::string& s) { return s; }

uint64_t NowMs()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace net
