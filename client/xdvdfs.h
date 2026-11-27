// Reads an Xbox 360 disc image (XDVDFS / GDFX) and copies every file out of it.
// The same walk as the test kit's extract_iso.ps1: plain file reads, no memory mapping.
#pragma once
#include <cstdint>
#include <functional>
#include <string>

namespace xdvdfs {
// iso and dest are UTF-8 paths. progress gets the bytes written so far and returns false to cancel.
// Returns true when every file was written; error says why not.
bool Extract(const std::string& iso, const std::string& dest, std::string& error,
             const std::function<bool(uint64_t written, uint64_t total)>& progress);
}
