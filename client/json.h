// TTT2 Client - a small JSON reader and writer for the server protocol (objects, arrays, strings, numbers, bools,
// null). Values are read leniently: a missing or wrongly typed field reads as empty / 0 / false.
#pragma once
#include <cstdio>
#include <cstdlib>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace json {

struct Value {
    enum Type { NUL, BOOL, NUM, STR, ARR, OBJ } type = NUL;
    bool b = false;
    double n = 0;
    std::string s;
    std::vector<Value> a;
    std::map<std::string, Value> o;

    const Value& operator[](const std::string& k) const
    {
        static const Value none;
        if (type != OBJ) return none;
        auto it = o.find(k);
        return it == o.end() ? none : it->second;
    }
    std::string Str() const { return type == STR ? s : std::string(); }
    double Num() const { return type == NUM ? n : 0; }
    int Int() const { return int(Num()); }
    bool Bool() const { return type == BOOL && b; }
};

namespace detail {
inline void Ws(const std::string& t, size_t& i) { while (i < t.size() && (t[i] == ' ' || t[i] == '\t' || t[i] == '\n' || t[i] == '\r')) ++i; }
inline void PutUtf8(std::string& out, unsigned cp)
{
    if (cp < 0x80) out += char(cp);
    else if (cp < 0x800) { out += char(0xC0 | (cp >> 6)); out += char(0x80 | (cp & 0x3F)); }
    else { out += char(0xE0 | (cp >> 12)); out += char(0x80 | ((cp >> 6) & 0x3F)); out += char(0x80 | (cp & 0x3F)); }
}
inline bool Parse(const std::string& t, size_t& i, Value& v, int depth)
{
    if (depth > 32) return false;
    Ws(t, i);
    if (i >= t.size()) return false;
    const char c = t[i];
    if (c == '{') {
        v.type = Value::OBJ;
        ++i;
        Ws(t, i);
        if (i < t.size() && t[i] == '}') { ++i; return true; }
        for (;;) {
            Value key;
            Ws(t, i);
            if (!Parse(t, i, key, depth + 1) || key.type != Value::STR) return false;
            Ws(t, i);
            if (i >= t.size() || t[i] != ':') return false;
            ++i;
            Value val;
            if (!Parse(t, i, val, depth + 1)) return false;
            v.o[key.s] = std::move(val);
            Ws(t, i);
            if (i < t.size() && t[i] == ',') { ++i; continue; }
            if (i < t.size() && t[i] == '}') { ++i; return true; }
            return false;
        }
    }
    if (c == '[') {
        v.type = Value::ARR;
        ++i;
        Ws(t, i);
        if (i < t.size() && t[i] == ']') { ++i; return true; }
        for (;;) {
            Value val;
            if (!Parse(t, i, val, depth + 1)) return false;
            v.a.push_back(std::move(val));
            Ws(t, i);
            if (i < t.size() && t[i] == ',') { ++i; continue; }
            if (i < t.size() && t[i] == ']') { ++i; return true; }
            return false;
        }
    }
    if (c == '"') {
        v.type = Value::STR;
        for (++i; i < t.size(); ++i) {
            if (t[i] == '"') { ++i; return true; }
            if (t[i] != '\\') { v.s += t[i]; continue; }
            if (++i >= t.size()) return false;
            switch (t[i]) {
            case 'n': v.s += '\n'; break;
            case 't': v.s += '\t'; break;
            case 'r': v.s += '\r'; break;
            case 'b': v.s += '\b'; break;
            case 'f': v.s += '\f'; break;
            case 'u':
                if (i + 4 >= t.size()) return false;
                PutUtf8(v.s, unsigned(std::strtoul(t.substr(i + 1, 4).c_str(), nullptr, 16)));
                i += 4;
                break;
            default: v.s += t[i];
            }
        }
        return false;
    }
    if (t.compare(i, 4, "true") == 0) { v.type = Value::BOOL; v.b = true; i += 4; return true; }
    if (t.compare(i, 5, "false") == 0) { v.type = Value::BOOL; i += 5; return true; }
    if (t.compare(i, 4, "null") == 0) { i += 4; return true; }
    char* end = nullptr;
    v.n = std::strtod(t.c_str() + i, &end);
    if (end == t.c_str() + i) return false;
    v.type = Value::NUM;
    i = size_t(end - t.c_str());
    return true;
}
} // namespace detail

inline bool Parse(const std::string& text, Value& v)
{
    size_t i = 0;
    v = {};
    return detail::Parse(text, i, v, 0);
}

inline std::string Quote(const std::string& s)
{
    std::string o = "\"";
    for (unsigned char c : s) {
        if (c == '"' || c == '\\') { o += '\\'; o += char(c); }
        else if (c < 0x20) { char b[8]; std::snprintf(b, sizeof b, "\\u%04x", c); o += b; }
        else o += char(c);
    }
    return o + "\"";
}

} // namespace json
