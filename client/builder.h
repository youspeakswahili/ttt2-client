// TTT2 Client - the game builder: fetches the official port release from its author's GitHub, runs its
// installer on the player's own disc image and Title Update 3 (no UI, Windows asks for admin once), then
// adds the controller layer. The client carries no game files and no part of the port.
#pragma once
#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

struct BuildRequest {
    std::string iso, tu, dest, dlc;
    bool onlyLayer = false;   // just (re)install the controller layer into dest
};

class Builder {
public:
    static constexpr const char* kRepo = "shihabal3amri/tekken-tag-tournament-2-pc";

    ~Builder();
    void Start(const BuildRequest& r);
    bool Running() const { return running_; }
    bool Finished() const { return finished_; }
    bool Succeeded() const { return ok_; }
    void Acknowledge() { finished_ = false; }

    // progress: 0..1 overall, the current step's text, and the log lines so far
    float Progress() const { return progress_; }
    std::string Status() const { std::lock_guard l(m_); return status_; }
    std::vector<std::string> Log() const { std::lock_guard l(m_); return log_; }
    std::string InstalledTag() const { std::lock_guard l(m_); return tag_; }

    // Latest release check (separate, quick): fills tag or error; runs on its own thread.
    void CheckLatest();
    bool Checking() const { return checking_; }
    std::string LatestTag() const { std::lock_guard l(m_); return latest_; }
    std::string CheckError() const { std::lock_guard l(m_); return checkError_; }

    // Call every frame: follows the port installer's log while it runs.
    void Poll();

private:
    void Run(BuildRequest r);
    void Say(const std::string& s, float p);
    void Line(const std::string& s);
    static bool ParseRelease(const std::string& json, std::string& tag, std::string& setupUrl);

    mutable std::mutex m_;
    std::thread t_, check_;
    std::atomic<bool> running_{ false }, finished_{ false }, ok_{ false }, checking_{ false }, installing_{ false };
    std::atomic<float> progress_{ 0 };
    std::string status_, tag_, latest_, checkError_;
    std::vector<std::string> log_;
    std::string setupLog_;
    size_t setupLogSeen_ = 0;
};
