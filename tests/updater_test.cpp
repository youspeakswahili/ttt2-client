// Checks Updater::VerifyZip against zips made by tools/release with the real signing key.
#include "../client/updater.h"
#include "../client/platform.h"
#include <cassert>
#include <cstdio>
#include <fstream>
#include <sstream>
static std::string Read(const char* p) { std::ifstream f(p, std::ios::binary); std::stringstream s; s << f.rdbuf(); return s.str(); }
int main(int argc, char** argv) {
    std::vector<Updater::File> files;
    std::string err;
    const std::string good = Read(argv[1]);
    assert(Updater::VerifyZip(good, "9.9.9", files, err));
    assert(files.size() == 2);
    std::printf("good zip: ok (%zu files)\n", files.size());
    assert(!Updater::VerifyZip(good, "9.9.8", files, err)); std::printf("wrong version: rejected (%s)\n", err.c_str());
    assert(!Updater::VerifyZip(Read(argv[2]), "9.9.9", files, err)); std::printf("changed file: rejected (%s)\n", err.c_str());
    assert(!Updater::VerifyZip(Read(argv[3]), "9.9.9", files, err)); std::printf("changed manifest: rejected (%s)\n", err.c_str());
    assert(!Updater::VerifyZip(Read(argv[4]), "9.9.9", files, err)); std::printf("other key: rejected (%s)\n", err.c_str());
    assert(!Updater::VerifyZip("not a zip", "9.9.9", files, err)); std::printf("garbage: rejected (%s)\n", err.c_str());
    assert(Updater::Newer("0.1.10", "0.1.9") && Updater::Newer("v0.2.0", "0.1.7") && !Updater::Newer("0.1.7", "0.1.7") && !Updater::Newer("0.1.6", "0.1.7"));
    std::puts("updater: all checks passed");
}
