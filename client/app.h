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

private:
    struct Impl;
    Impl* p_;
};
