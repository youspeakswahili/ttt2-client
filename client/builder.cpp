#include "builder.h"
#include "platform.h"
#include "../common/input_config.h"
#include <cstdio>

namespace {
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
bool EndsWith(const std::string& s, const std::string& e) { return s.size() >= e.size() && s.compare(s.size() - e.size(), e.size(), e) == 0; }
std::string Gb(uint64_t b) { char t[32]; std::snprintf(t, sizeof t, "%.1f GB", double(b) / (1ull << 30)); return t; }
} // namespace

Builder::~Builder()
{
    if (t_.joinable()) t_.detach();
    if (check_.joinable()) check_.detach();
}

bool Builder::ParseRelease(const std::string& json, std::string& tag, std::string& setupUrl)
{
    tag = JsonString(json, 0, "tag_name");
    setupUrl.clear();
    std::string fallback;
    size_t at = 0;
    for (;;) {
        size_t end = 0;
        const std::string url = JsonString(json, at, "browser_download_url", &end);
        if (url.empty()) break;
        at = end + 1;
        if (EndsWith(url, "/ttt2_setup.exe")) { setupUrl = url; break; }
        if (EndsWith(url, "setup.exe") && fallback.empty()) fallback = url;
    }
    if (setupUrl.empty()) setupUrl = fallback;
    return !tag.empty() && !setupUrl.empty();
}

void Builder::Say(const std::string& s, float p)
{
    {
        std::lock_guard l(m_);
        status_ = s;
        log_.push_back(s);
    }
    if (p >= 0) progress_ = p;
}
void Builder::Line(const std::string& s)
{
    std::lock_guard l(m_);
    log_.push_back(s);
}

void Builder::Start(const BuildRequest& r)
{
    if (running_) return;
    if (t_.joinable()) t_.join();
    {
        std::lock_guard l(m_);
        log_.clear();
        status_.clear();
    }
    progress_ = 0;
    ok_ = false;
    finished_ = false;
    running_ = true;
    t_ = std::thread([this, r] { Run(r); });
}

void Builder::CheckLatest()
{
    if (checking_) return;
    if (check_.joinable()) check_.join();
    checking_ = true;
    check_ = std::thread([this] {
        std::string body, err, tag, url;
        const bool ok = plat::HttpGet(std::string("https://api.github.com/repos/") + kRepo + "/releases/latest", body, err) &&
                        ParseRelease(body, tag, url);
        {
            std::lock_guard l(m_);
            latest_ = ok ? tag : "";
            checkError_ = ok ? "" : (err.empty() ? "No installer was found in the latest release." : err);
        }
        checking_ = false;
    });
}

void Builder::Run(BuildRequest r)
{
    auto fail = [&](const std::string& why) {
        Say("Stopped: " + why, -1);
        ok_ = false;
        running_ = false;
        finished_ = true;
    };
    const std::string layer = plat::JoinPath(plat::ExeDir(), "xinput9_1_0.dll");

    if (!r.onlyLayer) {
        if (!plat::FileExists(r.iso)) return fail("the disc image was not found: " + r.iso);
        if (!plat::FileExists(r.tu) && !plat::DirExists(r.tu)) return fail("the title update was not found: " + r.tu);
        if (r.dest.empty()) return fail("choose an install folder.");
        const uint64_t free = plat::FreeBytes(r.dest);
        if (free && free < (14ull << 30)) return fail("the drive has " + Gb(free) + " free; the game needs about 13 GB.");

        Say("Finding the latest version of the port", 0.02f);
        std::string body, err, tag, url;
        if (!plat::HttpGet(std::string("https://api.github.com/repos/") + kRepo + "/releases/latest", body, err))
            return fail("could not reach GitHub (" + err + ").");
        if (!ParseRelease(body, tag, url)) return fail("the latest release has no installer.");
        Line("Release " + tag + ": " + url);

        const std::string setup = plat::JoinPath(plat::TempDir(), "ttt2_setup.exe");
        Say("Downloading the port " + tag, 0.04f);
        if (!plat::HttpDownload(url, setup, err, [this](uint64_t done, uint64_t total) {
                if (total) progress_ = 0.04f + 0.08f * float(double(done) / double(total));
            }))
            return fail("the download failed (" + err + ").");

        Say("Building the game from your disc image. Windows will ask for permission.", 0.12f);
        Line("This usually takes a few minutes.");
        {
            std::lock_guard l(m_);
            setupLog_ = setup + ".log";
            setupLogSeen_ = 0;
        }
        installing_ = true;
        std::string args = "/silent \"" + r.iso + "\" \"" + r.tu + "\" \"" + r.dest + "\"";
        if (!r.dlc.empty()) args += " \"" + r.dlc + "\"";
        const int rc = plat::RunElevatedAndWait(setup, args, err);
        installing_ = false;
        Poll();   // the installer's last lines
        if (rc == -1) return fail(err.empty() ? "the installer could not start." : err);
        if (rc != 0) return fail("the port's installer reported a problem (see the lines above).");
        if (!plat::LooksLikeInstall(r.dest)) return fail("the installer finished but the game is not in " + r.dest + ".");
        std::lock_guard l(m_);
        tag_ = tag;
    }

    Say("Adding the controller layer", 0.96f);
    if (!plat::FileExists(layer)) return fail("xinput9_1_0.dll is missing next to the client. Extract the whole client zip.");
    std::string err;
    if (!plat::CopyFileElevated(layer, plat::JoinPath(r.dest, "xinput9_1_0.dll"), err)) return fail("could not add the controller layer (" + err + ").");
    Say(r.onlyLayer ? "Controller layer installed." : "Done. The game is ready.", 1.0f);
    ok_ = true;
    running_ = false;
    finished_ = true;
}

void Builder::Poll()
{
    std::string path;
    size_t seen;
    {
        std::lock_guard l(m_);
        path = setupLog_;
        seen = setupLogSeen_;
    }
    if (path.empty()) return;
    static uint32_t frame = 0;
    if (installing_ && ++frame % 15) return;   // ~4 times a second while it runs
    std::string text;
    if (!plat::ReadText(path, text) || text.size() <= seen) return;
    size_t pos = seen;
    while (true) {
        const size_t nl = text.find('\n', pos);
        if (nl == std::string::npos) break;
        std::string line = text.substr(pos, nl - pos);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        pos = nl + 1;
        // "[ 45%] Expanding ..." -> 12%..95% of the whole build
        int pct = -1;
        if (line.size() > 6 && line[0] == '[' && line[5] == ']') pct = std::atoi(line.c_str() + 1);
        std::string shown = pct >= 0 ? ttt2::Trim(line.substr(6)) : line;
        if (!shown.empty() && shown[0] != '[') {
            std::lock_guard l(m_);
            status_ = shown;
            log_.push_back(shown);
        }
        if (pct >= 0) progress_ = 0.12f + 0.83f * float(pct) / 100.0f;
    }
    std::lock_guard l(m_);
    setupLogSeen_ = pos;
}
