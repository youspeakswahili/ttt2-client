#include "updater.h"
#include "platform.h"
#include "update_key.h"
#include "version.h"
#include "../common/input_config.h"

extern "C" {
#include "monocypher-ed25519.h"
}
#include "miniz.h"

#include <cstring>

namespace {
const char kPrefix[] = "TTT2 Client/";

std::string JsonString(const std::string& json, size_t from, const char* key, size_t* at = nullptr)
{
    const std::string k = std::string("\"") + key + "\"";
    size_t p = json.find(k, from);
    if (p == std::string::npos) return {};
    p = json.find(':', p + k.size());
    if (p == std::string::npos) return {};
    p = json.find('"', p);
    if (p == std::string::npos) return {};
    std::string v;
    for (size_t i = p + 1; i < json.size(); ++i) {
        if (json[i] == '\\' && i + 1 < json.size()) { v += json[++i]; continue; }
        if (json[i] == '"') { if (at) *at = i; return v; }
        v += json[i];
    }
    return {};
}

bool FromHex(const std::string& h, uint8_t* out, size_t n)
{
    if (h.size() != n * 2) return false;
    for (size_t i = 0; i < n; ++i) {
        auto nib = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return -1;
        };
        const int a = nib(h[2 * i]), b = nib(h[2 * i + 1]);
        if (a < 0 || b < 0) return false;
        out[i] = uint8_t(a * 16 + b);
    }
    return true;
}

// A manifest path is relative, forward slashes, no "..", no drive or stream names.
bool SafePath(const std::string& p)
{
    if (p.empty() || p.size() > 200 || p[0] == '/' || p[0] == '\\') return false;
    if (p.find(':') != std::string::npos || p.find('\\') != std::string::npos) return false;
    size_t start = 0;
    while (start <= p.size()) {
        size_t end = p.find('/', start);
        if (end == std::string::npos) end = p.size();
        const std::string part = p.substr(start, end - start);
        if (part.empty() || part == "." || part == "..") return false;
        start = end + 1;
    }
    return true;
}

std::string Strip(std::string s) { return ttt2::Trim(s); }
} // namespace

Updater::~Updater()
{
    if (t_.joinable()) t_.detach();
}

bool Updater::Newer(const std::string& a, const std::string& b)
{
    auto parse = [](std::string v, int out[3]) {
        if (!v.empty() && (v[0] == 'v' || v[0] == 'V')) v.erase(0, 1);
        out[0] = out[1] = out[2] = 0;
        int i = 0;
        for (size_t p = 0; i < 3 && p <= v.size();) {
            size_t e = v.find('.', p);
            if (e == std::string::npos) e = v.size();
            out[i++] = std::atoi(v.substr(p, e - p).c_str());
            p = e + 1;
        }
    };
    int x[3], y[3];
    parse(a, x);
    parse(b, y);
    for (int i = 0; i < 3; ++i)
        if (x[i] != y[i]) return x[i] > y[i];
    return false;
}

void Updater::Fail(const std::string& e)
{
    {
        std::lock_guard l(m_);
        error_ = e;
    }
    state_ = FAILED;
}

void Updater::Check()
{
    const State s = state_;
    if (s == CHECKING || s == DOWNLOADING || s == READY) return;
    if (t_.joinable()) t_.join();
    state_ = CHECKING;
    t_ = std::thread([this] {
        std::string body, err;
        // every release, newest first (pre-releases included: the alpha's are); none published yet is up to date
        if (!plat::HttpGet(std::string("https://api.github.com/repos/") + kRepo + "/releases?per_page=1", body, err))
            return Fail("Could not check for updates (" + err + ").");
        if (body.find("tag_name") == std::string::npos && body.find('[') != std::string::npos) {
            state_ = UP_TO_DATE;
            return;
        }
        const std::string tag = JsonString(body, 0, "tag_name");
        std::string url;
        size_t at = 0;
        for (;;) {
            size_t end = 0;
            const std::string u = JsonString(body, at, "browser_download_url", &end);
            if (u.empty()) break;
            at = end + 1;
            const std::string name = u.substr(u.find_last_of('/') + 1);
            if (name.rfind("TTT2-Client-", 0) == 0 && name.size() > 4 && name.compare(name.size() - 4, 4, ".zip") == 0) { url = u; break; }
        }
        if (tag.empty()) return Fail("The latest release could not be read.");
        {
            std::lock_guard l(m_);
            latest_ = tag[0] == 'v' ? tag.substr(1) : tag;
            zipUrl_ = url;
        }
        state_ = (!url.empty() && Newer(tag, TTT2_CLIENT_VERSION)) ? AVAILABLE : UP_TO_DATE;
    });
}

bool Updater::VerifyZip(const std::string& zip, const std::string& expectVersion, std::vector<File>& files, std::string& error)
{
    files.clear();
    mz_zip_archive za;
    std::memset(&za, 0, sizeof za);
    if (!mz_zip_reader_init_mem(&za, zip.data(), zip.size(), 0)) { error = "The download is not a valid zip."; return false; }
    auto extract = [&](const std::string& name, std::string& out) -> bool {
        const int i = mz_zip_reader_locate_file(&za, (kPrefix + name).c_str(), nullptr, 0);
        if (i < 0) return false;
        size_t n = 0;
        void* p = mz_zip_reader_extract_to_heap(&za, mz_uint(i), &n, 0);
        if (!p) return false;
        out.assign(static_cast<const char*>(p), n);
        mz_free(p);
        return true;
    };
    std::string manifest, sigHex;
    bool ok = extract("manifest.txt", manifest) && extract("manifest.sig", sigHex);
    uint8_t sig[64];
    if (!ok || !FromHex(Strip(sigHex), sig, 64)) {
        error = "The update is not signed.";
    } else if (crypto_ed25519_check(sig, kReleasePublicKey, reinterpret_cast<const uint8_t*>(manifest.data()), manifest.size()) != 0) {
        error = "The update's signature is wrong. It was not installed.";
        ok = false;
    } else {
        // first line: "ttt2-client <version>"; then "<sha512 hex> <size> <path>"
        size_t pos = 0, line = 0;
        while (ok && pos < manifest.size()) {
            size_t nl = manifest.find('\n', pos);
            if (nl == std::string::npos) nl = manifest.size();
            const std::string l = Strip(manifest.substr(pos, nl - pos));
            pos = nl + 1;
            if (l.empty()) continue;
            if (line++ == 0) {
                if (l != "ttt2-client " + expectVersion) { error = "The update is for a different version (" + l + ")."; ok = false; }
                continue;
            }
            const size_t s1 = l.find(' '), s2 = s1 == std::string::npos ? s1 : l.find(' ', s1 + 1);
            if (s2 == std::string::npos) { error = "The update's file list is damaged."; ok = false; break; }
            uint8_t want[64];
            const std::string path = l.substr(s2 + 1);
            const unsigned long long size = std::strtoull(l.substr(s1 + 1, s2 - s1 - 1).c_str(), nullptr, 10);
            if (!FromHex(l.substr(0, s1), want, 64) || !SafePath(path)) { error = "The update's file list is damaged."; ok = false; break; }
            File f;
            f.path = path;
            if (!extract(path, f.data)) { error = "The update is missing " + path + "."; ok = false; break; }
            if (f.data.size() != size) { error = path + " in the update was changed. It was not installed."; ok = false; break; }
            uint8_t got[64];
            crypto_sha512(got, reinterpret_cast<const uint8_t*>(f.data.data()), f.data.size());
            if (std::memcmp(got, want, 64) != 0) { error = path + " in the update was changed. It was not installed."; ok = false; break; }
            files.push_back(std::move(f));
        }
        if (ok && files.empty()) { error = "The update has no files."; ok = false; }
    }
    mz_zip_reader_end(&za);
    if (!ok) files.clear();
    return ok;
}

void Updater::Download()
{
    if (state_ != AVAILABLE && state_ != FAILED) return;
    if (t_.joinable()) t_.join();
    state_ = DOWNLOADING;
    progress_ = 0;
    t_ = std::thread([this] {
        std::string url, version, err;
        {
            std::lock_guard l(m_);
            url = zipUrl_;
            version = latest_;
        }
        if (url.empty()) return Fail("There is no update to download.");
        const std::string zipPath = plat::JoinPath(plat::TempDir(), "TTT2-Client-update.zip");
        if (!plat::HttpDownload(url, zipPath, err, [this](uint64_t done, uint64_t total) {
                if (total) progress_ = float(double(done) / double(total));
            }))
            return Fail("The download failed (" + err + ").");
        std::string zip;
        if (!plat::ReadBinary(zipPath, zip)) return Fail("The download could not be read.");
        std::vector<File> files;
        if (!VerifyZip(zip, version, files, err)) return Fail(err);
        {
            std::lock_guard l(m_);
            staged_ = std::move(files);
        }
        progress_ = 1;
        state_ = READY;
    });
}

bool Updater::Apply(std::string& error)
{
    if (state_ != READY) { error = "Nothing to install."; return false; }
    if (t_.joinable()) t_.join();
    const std::string dir = plat::ExeDir();
    std::vector<std::pair<std::string, bool>> done;   // target, had an old copy
    auto rollback = [&] {
        for (auto it = done.rbegin(); it != done.rend(); ++it) {
            plat::DeleteFileAt(it->first);
            if (it->second) plat::MoveFileReplace(it->first + ".old", it->first);
        }
    };
    std::vector<File> files;
    {
        std::lock_guard l(m_);
        files = staged_;
    }
    for (const File& f : files) {
        std::string target = dir;
        size_t s = 0;
        for (size_t e; (e = f.path.find('/', s)) != std::string::npos; s = e + 1) {
            target = plat::JoinPath(target, f.path.substr(s, e - s));
            plat::MakeDir(target);
        }
        target = plat::JoinPath(target, f.path.substr(s));
        const bool had = plat::FileExists(target);
        if (had && !plat::MoveFileReplace(target, target + ".old")) {
            rollback();
            error = "Could not replace " + f.path + ". Is the client's folder read-only?";
            return false;
        }
        if (!plat::WriteBinary(target, f.data)) {
            if (had) plat::MoveFileReplace(target + ".old", target);
            rollback();
            error = "Could not write " + f.path + ".";
            return false;
        }
        done.push_back({ target, had });
    }
    return true;
}

void Updater::CleanUpOldFiles()
{
    for (const std::string& f : plat::ListFilesRecursive(plat::ExeDir()))
        if (f.size() > 4 && f.compare(f.size() - 4, 4, ".old") == 0) plat::DeleteFileAt(f);
}
