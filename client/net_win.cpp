// TTT2 Client - net.h on Windows: WinHTTP's WebSocket with the server's certificate pinned, Winsock UDP, DPAPI.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <winhttp.h>
#include <wincrypt.h>
#include "net.h"

#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <deque>
#include <mutex>
#include <thread>
#include <vector>

namespace net {
namespace {

std::wstring W(const std::string& s)
{
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring w(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), w.data(), n);
    return w;
}

std::string Hex(const uint8_t* p, size_t n)
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

class WinWs : public Ws {
public:
    HINTERNET session = nullptr, connect = nullptr, ws = nullptr;
    std::thread reader;
    std::mutex m;
    std::condition_variable cv;
    std::deque<std::string> inbox;
    bool closed = false;
    std::string error;
    std::mutex sendLock;

    ~WinWs() override { Close(); }

    void Start()
    {
        reader = std::thread([this] {
            std::vector<char> buf(16384);
            std::string msg;
            for (;;) {
                DWORD got = 0;
                WINHTTP_WEB_SOCKET_BUFFER_TYPE type{};
                const DWORD rc = WinHttpWebSocketReceive(ws, buf.data(), DWORD(buf.size()), &got, &type);
                if (rc != NO_ERROR || type == WINHTTP_WEB_SOCKET_CLOSE_BUFFER_TYPE) {
                    std::lock_guard l(m);
                    closed = true;
                    if (error.empty()) error = rc == NO_ERROR ? "The server closed the connection." : "The connection was lost (" + std::to_string(rc) + ").";
                    cv.notify_all();
                    return;
                }
                msg.append(buf.data(), got);
                if (msg.size() > (1u << 20)) msg.clear();   // nothing in this protocol is that big
                if (type == WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE || type == WINHTTP_WEB_SOCKET_BINARY_MESSAGE_BUFFER_TYPE) {
                    std::lock_guard l(m);
                    inbox.push_back(std::move(msg));
                    msg.clear();
                    cv.notify_all();
                }
            }
        });
    }
    bool Send(const std::string& text) override
    {
        std::lock_guard l(sendLock);
        if (!ws) return false;
        return WinHttpWebSocketSend(ws, WINHTTP_WEB_SOCKET_UTF8_MESSAGE_BUFFER_TYPE, const_cast<char*>(text.data()), DWORD(text.size())) == NO_ERROR;
    }
    int Recv(std::string& text, int timeoutMs) override
    {
        std::unique_lock l(m);
        cv.wait_for(l, std::chrono::milliseconds(timeoutMs), [&] { return !inbox.empty() || closed; });
        if (!inbox.empty()) {
            text = std::move(inbox.front());
            inbox.pop_front();
            return 1;
        }
        return closed ? -1 : 0;
    }
    std::string Error() const override
    {
        std::lock_guard l(const_cast<std::mutex&>(m));
        return error;
    }
    void Close() override
    {
        {
            std::lock_guard l(sendLock);
            if (ws) {
                WinHttpWebSocketClose(ws, WINHTTP_WEB_SOCKET_SUCCESS_CLOSE_STATUS, nullptr, 0);
                WinHttpCloseHandle(ws);   // unblocks the reader
                ws = nullptr;
            }
        }
        if (reader.joinable()) reader.join();
        if (connect) { WinHttpCloseHandle(connect); connect = nullptr; }
        if (session) { WinHttpCloseHandle(session); session = nullptr; }
        std::lock_guard l(m);
        closed = true;
    }
};

class WinUdp : public Udp {
public:
    SOCKET s = INVALID_SOCKET;
    ~WinUdp() override { if (s != INVALID_SOCKET) closesocket(s); }
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
        return sendto(s, static_cast<const char*>(data), int(size), 0, reinterpret_cast<sockaddr*>(&to), sizeof to) == int(size);
    }
    int Recv(std::string& from, std::string& data, int timeoutMs) override
    {
        fd_set set;
        FD_ZERO(&set);
        FD_SET(s, &set);
        timeval tv{ timeoutMs / 1000, (timeoutMs % 1000) * 1000 };
        const int r = select(0, &set, nullptr, nullptr, &tv);
        if (r <= 0) return r == 0 ? 0 : -1;
        char buf[2048];
        sockaddr_in a{};
        int alen = sizeof a;
        const int n = recvfrom(s, buf, sizeof buf, 0, reinterpret_cast<sockaddr*>(&a), &alen);
        if (n < 0) return WSAGetLastError() == WSAECONNRESET ? 0 : -1;   // an ICMP "port unreachable" from a punch: ignore
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
    auto c = std::make_unique<WinWs>();
    c->session = WinHttpOpen(L"TTT2Client", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!c->session) c->session = WinHttpOpen(L"TTT2Client", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!c->session) { error = "Networking is not available."; return nullptr; }
    WinHttpSetTimeouts(c->session, 5000, 5000, 8000, 8000);
    c->connect = WinHttpConnect(c->session, W(host).c_str(), INTERNET_PORT(port), 0);
    HINTERNET req = c->connect ? WinHttpOpenRequest(c->connect, L"GET", W(path).c_str(), nullptr, WINHTTP_NO_REFERER,
                                                    WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE) : nullptr;
    if (!req) { error = "Could not reach the server."; return nullptr; }
    // The certificate is self-signed: Windows' own checks are off, and its fingerprint is checked below instead.
    DWORD flags = SECURITY_FLAG_IGNORE_UNKNOWN_CA | SECURITY_FLAG_IGNORE_CERT_DATE_INVALID | SECURITY_FLAG_IGNORE_CERT_CN_INVALID |
                  SECURITY_FLAG_IGNORE_CERT_WRONG_USAGE;
    WinHttpSetOption(req, WINHTTP_OPTION_SECURITY_FLAGS, &flags, sizeof flags);
    WinHttpSetOption(req, WINHTTP_OPTION_UPGRADE_TO_WEB_SOCKET, nullptr, 0);
    bool ok = WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(req, nullptr);
    if (!ok) {
        error = "Could not reach the server (" + std::to_string(GetLastError()) + ").";
        WinHttpCloseHandle(req);
        return nullptr;
    }
    PCCERT_CONTEXT cert = nullptr;
    DWORD sz = sizeof cert;
    std::string fp;
    if (WinHttpQueryOption(req, WINHTTP_OPTION_SERVER_CERT_CONTEXT, &cert, &sz) && cert) {
        BYTE sum[32];
        DWORD sumLen = sizeof sum;
        if (CryptHashCertificate2(BCRYPT_SHA256_ALGORITHM, 0, nullptr, cert->pbCertEncoded, cert->cbCertEncoded, sum, &sumLen) && sumLen == 32)
            fp = Hex(sum, 32);
        CertFreeCertificateContext(cert);
    }
    if (fp.empty() || _stricmp(fp.c_str(), fingerprint.c_str()) != 0) {
        error = "The server's identity did not match. Not connecting.";
        WinHttpCloseHandle(req);
        return nullptr;
    }
    DWORD status = 0, slen = sizeof status;
    WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, nullptr, &status, &slen, nullptr);
    if (status != 101) {
        error = "The server answered " + std::to_string(status) + ".";
        WinHttpCloseHandle(req);
        return nullptr;
    }
    c->ws = WinHttpWebSocketCompleteUpgrade(req, 0);
    WinHttpCloseHandle(req);
    if (!c->ws) { error = "The server did not open the connection."; return nullptr; }
    c->Start();
    return c;
}

std::unique_ptr<Udp> UdpOpen(std::string& error)
{
    static bool started = [] { WSADATA d; return WSAStartup(MAKEWORD(2, 2), &d) == 0; }();
    if (!started) { error = "Networking is not available."; return nullptr; }
    auto u = std::make_unique<WinUdp>();
    u->s = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (u->s == INVALID_SOCKET) { error = "Could not open a UDP socket."; return nullptr; }
    sockaddr_in any{};
    any.sin_family = AF_INET;
    if (bind(u->s, reinterpret_cast<sockaddr*>(&any), sizeof any) != 0) { error = "Could not open a UDP socket."; return nullptr; }
    BOOL no = FALSE;   // SIO_UDP_CONNRESET off: an unreachable peer must not break the socket
    DWORD ret = 0;
    WSAIoctl(u->s, _WSAIOW(IOC_VENDOR, 12), &no, sizeof no, nullptr, 0, &ret, nullptr, nullptr);
    return u;
}

std::string Protect(const std::string& secret)
{
    DATA_BLOB in{ DWORD(secret.size()), reinterpret_cast<BYTE*>(const_cast<char*>(secret.data())) }, out{};
    if (!CryptProtectData(&in, L"TTT2 Client account", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    std::string r(reinterpret_cast<char*>(out.pbData), out.cbData);
    LocalFree(out.pbData);
    return r;
}
std::string Unprotect(const std::string& blob)
{
    DATA_BLOB in{ DWORD(blob.size()), reinterpret_cast<BYTE*>(const_cast<char*>(blob.data())) }, out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) return {};
    std::string r(reinterpret_cast<char*>(out.pbData), out.cbData);
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return r;
}

uint64_t NowMs()
{
    return uint64_t(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

} // namespace net
