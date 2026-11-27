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

std::string Builder::EngineDir()
{
    const std::string sub = plat::JoinPath(plat::ExeDir(), kEngineDir);
    if (plat::FileExists(plat::JoinPath(sub, plat::kGameExe))) return sub;
    return plat::ExeDir();
}
bool Builder::Extracted(const std::string& dir)
{
    return !dir.empty() && plat::FileExists(plat::JoinPath(dir, "game\\default.xex")) &&
           plat::FileExists(plat::JoinPath(dir, "game\\default.xexp"));
}

// rexglue.exe's own reason for stopping: the last "failed: ..." line it wrote into the setup log
static std::string LastFailure(const std::string& logFile, int rc)
{
    std::string text;
    plat::ReadText(logFile, text);
    const size_t at = text.rfind("failed: ");
    if (at == std::string::npos) return "code " + std::to_string(rc);
    size_t end = text.find_first_of("\r\n", at);
    return text.substr(at + 8, end == std::string::npos ? std::string::npos : end - at - 8);
}

void Builder::Run(BuildRequest r)
{
    auto fail = [&](const std::string& why) {
        Say("Stopped: " + why, -1);
        ok_ = false;
        running_ = false;
        finished_ = true;
    };
    // The game build ships inside the client folder ("game build"): ttt2.exe, its libraries and rexglue.exe.
    const std::string engine = EngineDir();
    const std::string rexglue = plat::JoinPath(engine, "rexglue.exe");
    if (!plat::FileExists(plat::JoinPath(engine, plat::kGameExe)) || !plat::FileExists(rexglue))
        return fail(std::string("the \"") + kEngineDir + "\" folder is missing next to the client. Extract the whole client zip.");
    if (r.dest.empty()) return fail("choose an install folder.");
    plat::MakeDir(r.dest);
    if (!plat::DirExists(r.dest)) return fail("could not make the folder " + r.dest + ". Pick one you can write to (not Program Files).");
    const std::string logFile = plat::JoinPath(r.dest, "setup log.txt");
    std::string err;

    if (!r.onlyLayer && Extracted(r.dest)) {
        Line("The game's files are already in " + r.dest + "; keeping them.");
    } else if (!r.onlyLayer) {
        if (!plat::FileExists(r.iso)) return fail("the disc image was not found: " + r.iso);
        if (!plat::FileExists(r.tu) && !plat::DirExists(r.tu)) return fail("the title update was not found: " + r.tu);
        const uint64_t free = plat::FreeBytes(r.dest);
        if (free && free < (9ull << 30)) return fail("the drive has " + Gb(free) + " free; the game needs about 8 GB.");

        // 1. the disc's files (the image itself is never changed)
        const std::string game = plat::JoinPath(r.dest, "game");
        Say("Copying the game's files out of your disc image (a few minutes)", 0.03f);
        const uint64_t expect = 7600ull << 20;   // about what the disc holds
        int rc = plat::RunHiddenAndWait(rexglue, "--log-file \"" + logFile + "\" vfs dump \"" + r.iso + "\" \"" + game + "\"",
                                        r.dest, err, [&] {
            const double done = double(plat::FolderBytes(game)) / double(expect);
            progress_ = 0.03f + 0.80f * float(done > 1 ? 1 : done);
        });
        if (rc == -1) return fail("could not start rexglue.exe (" + err + ").");
        if (rc != 0) return fail("reading the disc image failed: " + LastFailure(logFile, rc) + " (see \"setup log.txt\" in " + r.dest + ").");
        if (!plat::FileExists(plat::JoinPath(game, "default.xex"))) return fail("that disc image has no default.xex. Is it TTT2 for Xbox 360?");

        // 2. Title Update 3: its patch goes next to default.xex, the rest into update
        const std::string update = plat::JoinPath(r.dest, "update");
        Say("Reading Title Update 3", 0.84f);
        rc = plat::RunHiddenAndWait(rexglue, "--log-file \"" + logFile + "\" vfs dump \"" + r.tu + "\" \"" + update + "\"", r.dest, err, {});
        if (rc != 0) return fail("reading the title update failed: " + LastFailure(logFile, rc) + " (see \"setup log.txt\").");
        std::string patch;
        for (const auto& f : plat::ListFilesRecursive(update))
            if (EndsWith(f, ".xexp") || EndsWith(f, ".XEXP") || EndsWith(f, ".Xexp")) { patch = f; break; }
        if (patch.empty()) return fail("that title update has no .xexp patch in it. Use TU3 (TU_174Q22P_...).");
        if (!plat::CopyFileTo(patch, plat::JoinPath(game, "default.xexp"), err)) return fail("could not copy the patch (" + err + ").");
    }

    // 3. the game build itself (also how an updated client refreshes an installed game)
    Say("Adding the game build", 0.92f);
    const bool same = plat::JoinPath(engine, "x") == plat::JoinPath(r.dest, "x");   // the client lives in the game folder
    const auto files = same ? std::vector<std::string>{} : plat::ListFilesRecursive(engine);
    for (const auto& f : files) {
        const std::string rel = f.substr(engine.size() + 1);
        const std::string to = plat::JoinPath(r.dest, rel);
        const size_t slash = to.find_last_of("\\/");
        if (slash != std::string::npos) plat::MakeDir(to.substr(0, slash));
        if (!plat::CopyFileTo(f, to, err)) return fail("could not copy " + rel + " (" + err + "). Is the game still running?");
    }
    if (!plat::LooksLikeInstall(r.dest)) return fail("the game is not complete in " + r.dest + ".");
    {
        std::lock_guard l(m_);
        tag_ = kBuildTag;
    }
    Say(r.onlyLayer ? "Game build updated." : "Done. The game is ready.", 1.0f);
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
