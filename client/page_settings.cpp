// TTT2 Client - SETTINGS, in four tabs: sound and voice chat (live: the running game follows them), graphics and
// gameplay (read when the game starts), and this window.
#include "app_impl.h"

#include <cstdio>

using namespace ui;

namespace {
constexpr float kCtl = 400;
}

void App::Impl::PageSettings()
{
    PageHeader("SETTINGS", nullptr);
    static const char* tabs[] = { "Sound & voice", "Graphics", "Gameplay", "Client" };
    settingsTab = Tabs("settingstabs", settingsTab, tabs, 4);

    // a game.ini choice as segmented buttons; values: what each button stores (nullptr: its index)
    auto seg = [&](const char* sec, const char* key, int def, const char* const* labels, int n, const int* values = nullptr) {
        const int cur = game.GetInt(sec, key, def);
        int idx = 0;
        if (values) { for (int i = 0; i < n; ++i) if (values[i] == cur) idx = i; }
        else idx = std::clamp(cur, 0, n - 1);
        ImGui::PushID(key);
        if (Segmented(key, &idx, labels, n, kCtl)) { game.SetInt(sec, key, values ? values[idx] : idx); SaveGame(); if (gameRunning) Note("Saved. It applies the next time the game starts."); }
        ImGui::PopID();
    };
    // a percentage in client.ini that the running game follows (through the live file)
    auto livePercent = [&](const char* id, const char* sec, const char* key, int max) {
        int v = client.GetInt(sec, key, 100);
        if (Percent(id, &v, max, kCtl)) { client.SetInt(sec, key, v); liveDirty = true; }
        if (ImGui::IsItemDeactivatedAfterEdit()) { SaveClient(); WriteLive(); }
    };

    if (settingsTab == 0) {
        BeginCard("sound", 0, "Sound", "Applies straight away, while the game is running too.");
        RowLabel("Game volume", "Music, voices and effects.", kCtl);
        livePercent("##vol", "sound", "volume", 100);
        EndCard();
        Gap();
        BeginCard("voice", 0, "Voice chat", "Talk to your opponent in online matches. Every change applies straight away, mid-match too.");
        if (SDL_GetTicks() / 1000.0 - audioListAt > 5.0) RefreshAudio();
        static const char* modes[] = { "Off", "Push to talk", "Open mic" };
        static const char* modeKeys[] = { "off", "ptt", "open" };
        const std::string cur = client.Get("voice", "mode", "ptt");
        int m = cur == "off" ? 0 : cur == "open" ? 2 : 1;
        RowLabel("Voice chat", "Off: your microphone is never opened and your opponent isn't heard.", kCtl);
        if (Segmented("vmode", &m, modes, 3, kCtl)) { client.Set("voice", "mode", modeKeys[m]); SaveClient(); WriteLive(); }
        if (m != 0) {
            Divider();
            if (m == 1)
                KeyBindRow("key", "Push-to-talk key", "Hold it to talk while the game is in front. Click, then press any key or mouse button (Esc cancels).", 0x14);
            else
                KeyBindRow("mute_key", "Mute key", "Press it in the game to mute or unmute your microphone (MIC MUTED shows in the corner). Click, then press a key.", 0);
            Divider();
            RowLabel("Microphone", "The one you talk into.", kCtl);
            AudioCombo("##micdev", "mic_device", true, kCtl);
            Divider();
            RowLabel("Your microphone's level", "How loud you are for your opponent.", kCtl);
            livePercent("##mvol", "voice", "mic", 200);
            Divider();
            RowLabel("Hear your opponent on", "Headset, speakers...", kCtl);
            AudioCombo("##outdev", "out_device", false, kCtl);
            Divider();
            RowLabel("Your opponent's volume", "0% mutes them.", kCtl);
            livePercent("##vvol", "voice", "volume", 200);
        }
        EndCard();
    } else if (settingsTab == 1) {
        BeginCard("graphics", 0, "Graphics", "Read when the game starts.");
        struct Preset { const char* name; int scale, aa, aniso; };
        static const Preset presets[] = { { "Performance", 1, 0, 0 }, { "Original", 1, 1, -1 }, { "High", 2, 1, 4 }, { "Ultra", 3, 2, 5 } };
        const int scale = std::clamp(game.GetInt("Graphics", "Scale", 2), 1, 3), aa = game.GetInt("Graphics", "AA", 1), aniso = game.GetInt("Graphics", "Aniso", 4);
        int pick = 4;
        for (int i = 0; i < 4; ++i)
            if (presets[i].scale == scale && presets[i].aa == aa && presets[i].aniso == aniso) pick = i;
        RowLabel("Quality", "Performance: 720p, for older PCs and laptops - online, a PC that can't hold 60 fps slows the match down "
                            "for both players. Original: the Xbox 360's own 720p, smoothed. High: drawn at 1440p. Ultra: drawn at 4K.", kCtl);
        static const char* pn[] = { "Performance", "Original", "High", "Ultra", "Custom" };
        ImGui::PushID("preset");
        if (Segmented("preset", &pick, pn, 5, kCtl) && pick < 4) {
            game.SetInt("Graphics", "Scale", presets[pick].scale);
            game.SetInt("Graphics", "AA", presets[pick].aa);
            game.SetInt("Graphics", "Aniso", presets[pick].aniso);
            SaveGame();
        }
        ImGui::PopID();
        Divider();
        static const char* sc[] = { "720p", "1440p", "4K" };
        static const int scv[] = { 1, 2, 3 };
        RowLabel("Render resolution", "How many pixels the game is drawn with (1x, 2x or 3x the original 1280x720).", kCtl);
        seg("Graphics", "Scale", 2, sc, 3, scv);
        Divider();
        static const char* aan[] = { "Off", "FXAA", "FXAA extreme" };
        RowLabel("Anti-aliasing", "Smooths jagged edges.", kCtl);
        seg("Graphics", "AA", 1, aan, 3);
        Divider();
        static const char* an[] = { "Off", "Game's", "4x", "8x", "16x" };
        static const int anv[] = { 0, -1, 3, 4, 5 };
        RowLabel("Texture filtering", "Sharper floors and walls at an angle.", kCtl);
        seg("Graphics", "Aniso", 4, an, 5, anv);
        Divider();
        static const char* modes[] = { "Window", "Borderless", "Full screen" };
        RowLabel("Window mode", "Full screen takes the screen over; borderless switches windows faster.", kCtl);
        seg("Graphics", "Mode", 1, modes, 3);
        Divider();
        {
            int n = 0;
            SDL_DisplayID* ids = SDL_GetDisplays(&n);
            const int want = game.GetInt("Graphics", "Monitor", 0);
            std::string shown = "Windows' choice";
            if (want > 0 && want <= n) { const char* nm = SDL_GetDisplayName(ids[want - 1]); shown = std::to_string(want) + ": " + (nm ? nm : "Display"); }
            else if (want > 0) shown = "Not connected now - Windows' choice";
            RowLabel("Monitor", "The screen the game opens on.", kCtl);
            ImGui::SetNextItemWidth(kCtl);
            ImGui::PushFont(F.bold, 14.f);
            if (ImGui::BeginCombo("##monitor", shown.c_str())) {
                if (ImGui::Selectable("Windows' choice", want == 0)) { game.SetInt("Graphics", "Monitor", 0); SaveGame(); }
                for (int i = 0; i < n; ++i) {
                    const char* nm = SDL_GetDisplayName(ids[i]);
                    const std::string label = std::to_string(i + 1) + ": " + (nm ? nm : "Display");
                    if (ImGui::Selectable(label.c_str(), want == i + 1)) { game.SetInt("Graphics", "Monitor", i + 1); SaveGame(); }
                }
                ImGui::EndCombo();
            }
            ImGui::PopFont();
            SDL_free(ids);
        }
        Divider();
        static const char* onoff[] = { "On", "Off" };
        static const int onoffv[] = { 1, 0 };
        RowLabel("VSync", "Off can lower delay a little but may tear the picture.", kCtl);
        seg("Graphics", "VSync", 1, onoff, 2, onoffv);
        Divider();
        RowLabel("Variable refresh (G-Sync / FreeSync)", "Lets a VRR screen follow the game.", kCtl);
        seg("Graphics", "Tearing", 1, onoff, 2, onoffv);
        Divider();
        static const char* asp[] = { "16:9 (bars)", "Stretch" };
        RowLabel("Aspect ratio", "16:9 keeps the original shape on any screen.", kCtl);
        seg("Graphics", "Stretch", 0, asp, 2);
        Divider();
        static const char* shc[] = { "In the background", "Before drawing" };
        RowLabel("Shader compiling", "Each effect's shader is compiled the first time it's seen, then kept. In the background: no stutter, "
                                     "an effect may be missing for a moment. Before drawing: short hitches early on.", kCtl);
        seg("Graphics", "SyncShaders", 0, shc, 2);
        Divider();
        static const char* api[] = { "Direct3D 12", "Vulkan" };
        RowLabel("Renderer", "Direct3D 12 is the tested one; try Vulkan if the picture has problems.", kCtl);
        seg("Graphics", "Vulkan", 0, api, 2);
        Gap(4);
        Small(col::faint, "The game runs at 60 fps, locked: the speed it was made for.");
        EndCard();
    } else if (settingsTab == 2) {
        BeginCard("gameplay", 0, "Gameplay", "Read when the game starts.");
        static const char* l[] = { "Removed", "Original" };
        RowLabel("Offline input delay", "The original game holds every press 2 extra frames offline. Removed: about 33 ms faster.", kCtl);
        seg("Game", "InputDelay", 0, l, 2);
        Divider();
        static const char* n[] = { "Standard", "Prioritize response" };
        if (game.GetInt("Online", "Netcode", 0) > 1) { game.SetInt("Online", "Netcode", 0); SaveGame(); }
        RowLabel("Online netcode", "Standard: up to 2 frames of input delay, fewer corrections. Prioritize response: 1 frame less delay, a few more corrections.", kCtl);
        seg("Online", "Netcode", 0, n, 2);
        Divider();
        static const char* ov[] = { "On", "Off" };
        static const int ovv[] = { 1, 0 };
        RowLabel("Fight overlay", "Online: ping, input delay and rollback in the corner, both players' ratings under the health bars, MIC while you talk.", kCtl);
        seg("Game", "Overlay", 1, ov, 2, ovv);
        Divider();
        static const char* ul[] = { "Everything unlocked", "As saved" };
        static const int ulv[] = { 1, 0 };
        RowLabel("Unlocks", "Every character, costume, item and stage open, to customise your characters.", kCtl);
        seg("Game", "UnlockAll", 1, ul, 2, ulv);
        EndCard();
    } else {
        BeginCard("clientwin", 0, "This window");
        static const char* fs[] = { "Window", "Full screen" };
        int v = fullscreen ? 1 : 0;
        RowLabel("Client window", "F11 or Alt+Enter switches too.", kCtl);
        if (Segmented("clientfs", &v, fs, 2, kCtl)) SetFullscreen(v == 1);
        Gap(4);
        Small(col::faint, "The whole client works with a controller: D-pad or stick to move, A / Cross to choose.");
        EndCard();
    }
}
