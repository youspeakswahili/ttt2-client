// TTT2 Client - entry point: SDL3 window + renderer (Direct3D 11 on Windows), Dear ImGui.
// --shot <page 0..3> <file.bmp>: render one page to a picture and quit (UI checks).
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include "imgui.h"
#include "imgui_impl_sdl3.h"
#include "imgui_impl_sdlrenderer3.h"
#include "app.h"
#include "platform.h"
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    int shotPage = -1;
    const char* shotFile = nullptr;
    for (int i = 1; i + 2 < argc + 1; ++i)
        if (std::strcmp(argv[i], "--shot") == 0 && i + 2 < argc) { shotPage = std::atoi(argv[i + 1]); shotFile = argv[i + 2]; }

    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "0");   // presses in the game must not move the client
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11,direct3d12,opengl,software");
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "TTT2 Client", SDL_GetError(), nullptr);
        return 1;
    }
    float scale = shotFile ? 1.f : SDL_GetDisplayContentScale(SDL_GetPrimaryDisplay());
    if (scale <= 0) scale = 1;
    const int w = int(1280 * scale), h = int(800 * scale);
    SDL_Window* window = SDL_CreateWindow("TTT2 Client", w, h, SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIDDEN);
    if (!window) return 1;
    SDL_SetWindowMinimumSize(window, int(1100 * scale), int(680 * scale));
    SDL_Renderer* renderer = SDL_CreateRenderer(window, nullptr);
    if (!renderer) return 1;
    SDL_SetRenderVSync(renderer, 1);
    plat::StyleWindow(window);
    SDL_SetWindowPosition(window, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED);
    SDL_ShowWindow(window);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui_ImplSDL3_InitForSDLRenderer(window, renderer);
    ImGui_ImplSDLRenderer3_Init(renderer);

    // TTT2_FAKE_PAD=1 (UI checks): a virtual fight stick with 1 and the lever's down-forward held
    SDL_Joystick* fake = nullptr;
    if (shotFile && std::getenv("TTT2_FAKE_PAD")) {
        SDL_VirtualJoystickDesc desc;
        SDL_INIT_INTERFACE(&desc);
        desc.type = SDL_JOYSTICK_TYPE_ARCADE_STICK;
        desc.naxes = 2;
        desc.nbuttons = 12;
        desc.nhats = 1;
        desc.name = "Victrix Pro FS (PS5)";
        const SDL_JoystickID id = SDL_AttachVirtualJoystick(&desc);
        fake = SDL_OpenJoystick(id);
        SDL_SetJoystickVirtualButton(fake, 0, true);
        SDL_SetJoystickVirtualHat(fake, 0, SDL_HAT_RIGHTDOWN);
        SDL_UpdateJoysticks();
    }

    App app;
    app.Init(window, scale);
    if (shotPage >= 0) app.SetPage(shotPage);
    for (int i = 1; i < argc; ++i)
        if (std::strcmp(argv[i], "--updated") == 0) app.JustUpdated();

    int frames = 0;
    bool running = true;
    while (running) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL3_ProcessEvent(&e);
            app.Event(e);
            if (e.type == SDL_EVENT_QUIT) running = false;
            if (e.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && e.window.windowID == SDL_GetWindowID(window)) running = false;
        }
        if (SDL_GetWindowFlags(window) & SDL_WINDOW_MINIMIZED) { SDL_Delay(50); continue; }
        ImGui_ImplSDLRenderer3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();
        app.Frame();
        if (app.WantsQuit()) running = false;
        ImGui::Render();
        SDL_SetRenderDrawColor(renderer, 9, 9, 12, 255);
        SDL_RenderClear(renderer);
        ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer);
        if (shotFile && ++frames == 6) {
            if (SDL_Surface* s = SDL_RenderReadPixels(renderer, nullptr)) { SDL_SaveBMP(s, shotFile); SDL_DestroySurface(s); }
            running = false;
        }
        SDL_RenderPresent(renderer);
    }

    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
