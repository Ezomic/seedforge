#pragma once

#include "ichor/platform/platform.h"

#include <functional>

struct SDL_Window;
union SDL_Event;

namespace ichor {

class SdlPlatform final : public Platform {
public:
    SdlPlatform() = default;
    ~SdlPlatform() override;

    bool init(const WindowDesc& desc) override;
    void pumpEvents() override;
    bool quitRequested() const override { return quit_; }
    double now() const override;
    Extent drawableSize() const override;
    InputFrame input() const override { return input_; }

    SDL_Window* window() const { return window_; }

    // The renderer's UI layer sees every event before the platform does.
    void setEventHook(std::function<void(const SDL_Event&)> hook) { hook_ = std::move(hook); }

private:
    SDL_Window* window_ = nullptr;
    bool quit_ = false;
    InputFrame input_;
    std::function<void(const SDL_Event&)> hook_;
};

}
