// Seeds a local server for screenshots: five accounts play ranked matches against each other.
#include "online.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/stat.h>
#include <thread>
static void Wait(Online& o, Online::State s) { for (int i = 0; i < 400 && o.Snapshot().state != s; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(25)); }
int main(int argc, char** argv) {
    ServerConfig cfg; cfg.host = "127.0.0.1"; cfg.port = std::atoi(argv[1]); cfg.fingerprint = argv[2];
    const std::string me = argv[3], others = argv[4];
    const char* names[] = { "Mumbai", "Heihachan", "TagMaster", "JunStan", "BlueFist" };
    Online* p[5];
    for (int i = 0; i < 5; ++i) {
        std::string d = i == 0 ? me : others + "/" + std::to_string(i);
        mkdir(d.c_str(), 0700);
        p[i] = new Online(cfg, d); p[i]->clientVersion = "0.2.0"; p[i]->Start();
        Wait(*p[i], Online::NEED_NAME); p[i]->Register(names[i]); Wait(*p[i], Online::LOBBY);
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(2500));
    for (int round = 0; round < 6; ++round)
        for (int a = 0; a < 5; a += 2) {
            const int b = (a + 1 + round) % 5; if (b == a) continue;
            p[a]->Queue("ranked", "any", 200); p[b]->Queue("ranked", "any", 200);
            Wait(*p[a], Online::READY); Wait(*p[b], Online::READY);
            const bool aWins = (a + round) % 3 != 0;
            p[a]->Report(aWins); p[b]->Report(!aWins);
            Wait(*p[a], Online::LOBBY); Wait(*p[b], Online::LOBBY);
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    std::printf("seeded: %s %d\n", p[0]->Snapshot().name.c_str(), p[0]->Snapshot().rating);
    std::fflush(stdout);
    std::_Exit(0);
}
