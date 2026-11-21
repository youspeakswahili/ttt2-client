// TTT2 Client - a small INI editor that keeps the file's other lines, comments and order as they are
// (the port's settings.ini is also written by the game itself).
#pragma once
#include <string>
#include <vector>
#include "../common/input_config.h"

class IniFile {
public:
    void Parse(const std::string& text)
    {
        lines_.clear();
        size_t pos = 0;
        while (pos < text.size()) {
            size_t nl = text.find('\n', pos);
            if (nl == std::string::npos) nl = text.size();
            std::string l = text.substr(pos, nl - pos);
            if (!l.empty() && l.back() == '\r') l.pop_back();
            lines_.push_back(l);
            pos = nl + 1;
        }
    }
    std::string Text() const
    {
        std::string t;
        for (auto& l : lines_) t += l + "\r\n";
        return t;
    }
    std::string Get(const std::string& sec, const std::string& key, const std::string& def = "") const
    {
        const int i = Find(sec, key);
        if (i < 0) return def;
        const std::string& l = lines_[size_t(i)];
        return ttt2::Trim(l.substr(l.find('=') + 1));
    }
    int GetInt(const std::string& sec, const std::string& key, int def) const
    {
        const std::string v = Get(sec, key);
        if (v.empty()) return def;
        char* end = nullptr;
        const long n = std::strtol(v.c_str(), &end, 10);
        return end == v.c_str() ? def : int(n);
    }
    void Set(const std::string& sec, const std::string& key, const std::string& value)
    {
        const int i = Find(sec, key);
        if (i >= 0) { lines_[size_t(i)] = key + "=" + value; return; }
        int s = Section(sec);
        if (s < 0) {
            if (!lines_.empty() && !lines_.back().empty()) lines_.push_back("");
            lines_.push_back("[" + sec + "]");
            lines_.push_back(key + "=" + value);
            return;
        }
        size_t at = size_t(s) + 1;
        size_t last = at;   // after the section's last non-empty line
        for (; at < lines_.size(); ++at) {
            const std::string t = ttt2::Trim(lines_[at]);
            if (!t.empty() && t[0] == '[') break;
            if (!t.empty()) last = at + 1;
        }
        lines_.insert(lines_.begin() + long(last), key + "=" + value);
    }
    void SetInt(const std::string& sec, const std::string& key, int v) { Set(sec, key, std::to_string(v)); }

private:
    int Section(const std::string& sec) const
    {
        for (size_t i = 0; i < lines_.size(); ++i) {
            const std::string t = ttt2::Trim(lines_[i]);
            if (t.size() > 2 && t[0] == '[' && Lower(t.substr(1, t.find(']') - 1)) == Lower(sec)) return int(i);
        }
        return -1;
    }
    int Find(const std::string& sec, const std::string& key) const
    {
        const int s = Section(sec);
        if (s < 0) return -1;
        for (size_t i = size_t(s) + 1; i < lines_.size(); ++i) {
            const std::string t = ttt2::Trim(lines_[i]);
            if (!t.empty() && t[0] == '[') break;
            const size_t eq = t.find('=');
            if (eq != std::string::npos && Lower(ttt2::Trim(t.substr(0, eq))) == Lower(key)) return int(i);
        }
        return -1;
    }
    static std::string Lower(std::string s)
    {
        for (char& c : s) if (c >= 'A' && c <= 'Z') c = char(c - 'A' + 'a');
        return s;
    }
    std::vector<std::string> lines_;
};
