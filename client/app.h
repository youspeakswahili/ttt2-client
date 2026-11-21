#pragma once
#include <SDL3/SDL.h>

class App {
public:
    App();
    ~App();
    void Init(SDL_Window* window, float scale);
    void Event(const SDL_Event& e);
    void Frame();
    void SetPage(int page);   // 0 play, 1 controls, 2 settings, 3 install
    void JustUpdated();       // started by the updater: bring the game's controller layer up to date
    bool WantsQuit() const;   // an update was installed and the new client started

private:
    struct Impl;
    Impl* p_;
};
