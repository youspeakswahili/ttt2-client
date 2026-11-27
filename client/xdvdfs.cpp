#include "xdvdfs.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <vector>

namespace xdvdfs {
namespace {
namespace fs = std::filesystem;
constexpr uint64_t kSector = 2048;
constexpr char kMagic[] = "MICROSOFT*XBOX*MEDIA";

fs::path P(const std::string& utf8) { return fs::path(std::u8string(utf8.begin(), utf8.end())); }
uint16_t U16(const uint8_t* p) { return uint16_t(p[0] | p[1] << 8); }
uint32_t U32(const uint8_t* p) { return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24; }

struct Job {
    std::ifstream in;
    uint64_t partition = 0, imageSize = 0, total = 0, written = 0;
    std::vector<char> buf = std::vector<char>(4u << 20);
    std::string error;
    const std::function<bool(uint64_t, uint64_t)>* progress = nullptr;

    bool ReadAt(uint64_t off, void* to, size_t n)
    {
        in.clear();
        in.seekg(std::streamoff(off));
        in.read(static_cast<char*>(to), std::streamsize(n));
        return size_t(in.gcount()) == n;
    }
    bool Dir(uint64_t sector, uint64_t size, std::vector<uint8_t>& out)
    {
        if (size == 0) { out.clear(); return true; }
        if (size > (64u << 20)) { error = "a folder in the image is too large; the image looks damaged"; return false; }
        out.resize(size_t(size));
        if (!ReadAt(partition + sector * kSector, out.data(), out.size())) { error = "the image ends inside a folder; it looks cut short"; return false; }
        return true;
    }
    // every entry of a folder (a binary tree of entries, 4-byte aligned offsets)
    template <class F> bool Walk(uint64_t sector, uint64_t size, F&& each)
    {
        std::vector<uint8_t> d;
        if (!Dir(sector, size, d)) return false;
        std::vector<uint32_t> stack{ 0 };
        std::set<uint32_t> seen;
        while (!stack.empty()) {
            const uint32_t off = stack.back();
            stack.pop_back();
            if (off + 14 > d.size() || !seen.insert(off).second) continue;
            const uint8_t* e = d.data() + off;
            const uint16_t left = U16(e), right = U16(e + 2);
            if (left == 0xFFFF && right == 0xFFFF) continue;
            const uint8_t nameLen = e[13];
            if (off + 14u + nameLen > d.size()) continue;
            const std::string name(reinterpret_cast<const char*>(e + 14), nameLen);
            if (!each(name, U32(e + 4), U32(e + 8), (e[12] & 0x10) != 0)) return false;
            if (left) stack.push_back(uint32_t(left) * 4);
            if (right) stack.push_back(uint32_t(right) * 4);
        }
        return true;
    }
    static bool SafeName(const std::string& n)
    {
        if (n.empty() || n == "." || n == "..") return false;
        for (char c : n)
            if (c == '/' || c == '\\' || c == ':' || static_cast<unsigned char>(c) < 32) return false;
        return true;
    }
    bool Sum(uint64_t sector, uint64_t size, int depth)
    {
        if (depth > 32) { error = "the image's folders nest too deep; it looks damaged"; return false; }
        return Walk(sector, size, [&](const std::string&, uint32_t s, uint32_t z, bool dir) {
            if (dir) return Sum(s, z, depth + 1);
            total += z;
            return true;
        });
    }
    bool Copy(uint64_t sector, uint64_t size, const fs::path& to)
    {
        if (partition + sector * kSector + size > imageSize) { error = "the image ends before " + to.filename().string() + "; it looks cut short"; return false; }
        std::ofstream out(to, std::ios::binary | std::ios::trunc);
        if (!out) { error = "could not write " + to.string() + " (is the drive full or read-only?)"; return false; }
        in.clear();
        in.seekg(std::streamoff(partition + sector * kSector));
        for (uint64_t left = size; left;) {
            const size_t n = size_t(left < buf.size() ? left : buf.size());
            in.read(buf.data(), std::streamsize(n));
            if (size_t(in.gcount()) != n) { error = "reading the image failed inside " + to.filename().string(); return false; }
            if (!out.write(buf.data(), std::streamsize(n))) { error = "could not write " + to.string() + " (is the drive full?)"; return false; }
            left -= n;
            written += n;
            if (*progress && !(*progress)(written, total)) { error = "cancelled"; return false; }
        }
        out.close();
        if (!out) { error = "could not finish writing " + to.string() + " (is the drive full?)"; return false; }
        return true;
    }
    bool Out(uint64_t sector, uint64_t size, const fs::path& to, int depth)
    {
        std::error_code ec;
        fs::create_directories(to, ec);
        if (ec) { error = "could not make the folder " + to.string(); return false; }
        if (depth > 32) { error = "the image's folders nest too deep; it looks damaged"; return false; }
        return Walk(sector, size, [&](const std::string& name, uint32_t s, uint32_t z, bool dir) {
            if (!SafeName(name)) { error = "the image has a file name that can't be used: " + name; return false; }
            return dir ? Out(s, z, to / name, depth + 1) : Copy(s, z, to / name);
        });
    }
};
} // namespace

bool Extract(const std::string& iso, const std::string& dest, std::string& error,
             const std::function<bool(uint64_t, uint64_t)>& progress)
{
    Job j;
    j.progress = &progress;
    std::error_code ec;
    j.imageSize = fs::file_size(P(iso), ec);
    if (ec) { error = "the disc image could not be opened (" + ec.message() + ")"; return false; }
    j.in.open(P(iso), std::ios::binary);
    if (!j.in) { error = "the disc image could not be opened for reading"; return false; }
    // the game partition: XGD3, XGD2, original Xbox, or a trimmed image that is only the partition
    bool found = false;
    for (uint64_t p : { 0x2080000ull, 0xFD90000ull, 0x18300000ull, 0ull }) {
        char m[20];
        if (p + 0x10000 + 28 > j.imageSize || !j.ReadAt(p + 0x10000, m, sizeof m)) continue;
        if (std::memcmp(m, kMagic, sizeof m) == 0) { j.partition = p; found = true; break; }
    }
    if (!found) { error = "this is not an Xbox 360 disc image (no game partition found)"; return false; }
    uint8_t vd[28];
    if (!j.ReadAt(j.partition + 0x10000, vd, sizeof vd)) { error = "the image's volume header could not be read"; return false; }
    const uint32_t rootSector = U32(vd + 20), rootSize = U32(vd + 24);
    if (!j.Sum(rootSector, rootSize, 0) || !j.Out(rootSector, rootSize, P(dest), 0)) { error = j.error; return false; }
    if (j.written == 0) { error = "the image has no files in it"; return false; }
    return true;
}
} // namespace xdvdfs
