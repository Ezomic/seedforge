#pragma once

#include "ichor/platform/mouse_look.h"
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
    void requestQuit() override { quit_ = true; }
    void setEscapeQuits(bool quits) override { escapeQuits_ = quits; }
    double now() const override;
    Extent drawableSize() const override;
    InputFrame input() const override { return input_; }
    Pointer pointer() const override { return pointer_; }
    NavKeys navKeys() const override { return navKeys_; }
    void reserveArrowKeys(bool reserved) override { arrowsReserved_ = reserved; }
    void enableLook(bool enabled) override;
    void setMouseSensitivity(float sensitivity) override { mouseLook_.setSensitivity(sensitivity); }

    SDL_Window* window() const { return window_; }

    // The renderer's UI layer sees every event before the platform does.
    void setEventHook(std::function<void(const SDL_Event&)> hook) { hook_ = std::move(hook); }

private:
    SDL_Window* window_ = nullptr;
    bool quit_ = false;
    bool escapeQuits_ = true;
    InputFrame input_;
    Pointer pointer_;
    NavKeys navKeys_;
    bool arrowsReserved_ = false;
    bool lookEnabled_ = false;
    MouseLook mouseLook_;
    double lastPump_ = 0.0;
    std::function<void(const SDL_Event&)> hook_;
};

}
