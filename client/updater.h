// TTT2 Client - updates itself from the project's GitHub releases.
//
//  1. Check: GitHub's latest release; newer than this client and has a TTT2-Client-*.zip?
//  2. Download the zip and check it: manifest.sig must be the release key's signature of manifest.txt
//     (update_key.h), the manifest's version must be the release's, and every file must match its SHA-512.
//     Nothing unsigned or altered is ever written into the client's folder.
//  3. Apply: each file is moved in place (the old one renamed to *.old first, which also works for the running
//     exe), then the new client starts and this one quits. Any failure rolls back.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

class Updater {
public:
    static constexpr const char* kRepo = "youspeakswahili/ttt2-client";

    enum State { IDLE, CHECKING, UP_TO_DATE, AVAILABLE, DOWNLOADING, READY, FAILED };

    ~Updater();
    void Check();     // on its own thread
    void Download();  // on its own thread; READY when the files are checked and staged
    bool Apply(std::string& error);   // main thread: swap the files in; then the caller restarts

    State GetState() const { return state_; }
    float Progress() const { return progress_; }
    std::string Latest() const { std::lock_guard l(m_); return latest_; }
    std::string Error() const { std::lock_guard l(m_); return error_; }

    // Startup: remove the *.old files a previous update left behind.
    static void CleanUpOldFiles();
    // Is version a newer than b? ("0.1.10" > "0.1.9")
    static bool Newer(const std::string& a, const std::string& b);
    // Checks a downloaded release zip (in memory) and returns its files; exposed for tests.
    struct File { std::string path; std::string data; };
    static bool VerifyZip(const std::string& zip, const std::string& expectVersion, std::vector<File>& files, std::string& error);

private:
    void Fail(const std::string& e);
    mutable std::mutex m_;
    std::thread t_;
    std::atomic<State> state_{ IDLE };
    std::atomic<float> progress_{ 0 };
    std::string latest_, zipUrl_, error_;
    std::vector<File> staged_;
};
