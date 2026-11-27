#include "../common/input_config.h"
#include <cassert>
#include <cstdio>
using namespace ttt2;
int main() {
    InputConfig c; c.socdLR = LR_LAST; c.socdUD = UD_NEUTRAL; c.stick = STICK_DPAD; c.deadzone = 42;
    DeviceMap& d = c.Get("030000004c050000e60c000000006800"); d.name = "Victrix Pro FS";
    d.out[O_X] = { {S_BUTTON,0,0}, {S_BUTTON,6,0} }; d.out[O_Y] = { {S_BUTTON,6,0} };   // button 7 = 1+2 macro
    d.out[O_UP] = { {S_HAT,0,1} }; d.out[O_LT] = { {S_AXIS_POS,4,0} }; d.out[O_RT] = { {S_AXIS_NEG,5,0} };
    const std::string t = WriteConfig(c);
    InputConfig r; ParseConfig(t, r);
    assert(r.socdLR == LR_LAST && r.socdUD == UD_NEUTRAL && r.stick == STICK_DPAD && r.deadzone == 42);
    const DeviceMap* m = r.Find("030000004c050000e60c000000006800");
    assert(m && m->name == "Victrix Pro FS");
    assert(m->out[O_X].size() == 2 && m->out[O_X][1] == (Source{S_BUTTON,6,0}));
    assert(m->out[O_UP][0] == (Source{S_HAT,0,1}) && m->out[O_LT][0].kind == S_AXIS_POS && m->out[O_RT][0].kind == S_AXIS_NEG);
    assert(WriteConfig(r) == t);
    const uint16_t L = OutBit(O_LEFT), R = OutBit(O_RIGHT), U = OutBit(O_UP), D = OutBit(O_DOWN), A = OutBit(O_A);
    assert(CleanSocd(L|R|A, LR_NEUTRAL, UD_UP, O_LEFT, O_UP) == A);
    assert(CleanSocd(L|R, LR_LAST, UD_UP, O_RIGHT, O_UP) == R);
    assert(CleanSocd(L|R, LR_OFF, UD_UP, O_RIGHT, O_UP) == (L|R));
    assert(CleanSocd(U|D, LR_NEUTRAL, UD_UP, O_LEFT, O_DOWN) == U);
    assert(CleanSocd(U|D, LR_NEUTRAL, UD_NEUTRAL, O_LEFT, O_DOWN) == 0);
    assert(CleanSocd(U|D|L, LR_NEUTRAL, UD_LAST, O_LEFT, O_DOWN) == (D|L));
    DeviceMap g = GenericMap(12, 1);
    assert(g.out[O_X][0] == (Source{S_BUTTON,0,0}) && g.out[O_START][0] == (Source{S_BUTTON,9,0}) && g.out[O_LEFT][0].hatDir == 8);
    InputConfig bad; ParseConfig("[general]\ndeadzone=500\n[device x]\nA = zz, b3 ,h1q, a2*\n", bad);
    assert(bad.deadzone == 90 && bad.devices[0].out[O_A].size() == 1);
    assert(OutBit(O_M134) == (OutBit(O_X) | OutBit(O_A) | OutBit(O_B)) && std::string(OutKey(O_M134)) == "M134");
    assert(OutBit(O_M23) == (OutBit(O_Y) | OutBit(O_A)) && std::string(OutKey(O_M23)) == "M23");
    assert(OutBit(O_M14) == (OutBit(O_X) | OutBit(O_B)) && std::string(OutKey(O_M14)) == "M14");
    InputConfig mc; mc.Get("g").out[O_M134] = { {S_BUTTON,9,0} };
    InputConfig mr; ParseConfig(WriteConfig(mc), mr);
    assert(mr.Find("g") && mr.Find("g")->out[O_M134].size() == 1);
    std::puts("input_config: all checks passed");
}
