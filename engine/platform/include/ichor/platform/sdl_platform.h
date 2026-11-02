#pragma once

#include "ichor/platform/mouse_attack.h"
#include "ichor/platform/mouse_look.h"
#include "ichor/platform/platform.h"
#include "ichor/platform/typing_guard.h"

#include <functional>
#include <string>
#include <utility>

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
    bool overlayKey() const override { return overlayKey_; }
    bool helpKey() const override { return helpKey_; }
    bool soundLegendKey() const override { return soundLegendKey_; }
    bool captionKey() const override { return captionKey_; }
    bool noteKey() const override { return noteKey_; }
    void setTextEntry(bool on) override;
    std::string takeTypedText() override { return std::exchange(typed_, {}); }
    bool focused() const override { return focused_; }
    void reserveArrowKeys(bool reserved) override { arrowsReserved_ = reserved; }
    void enableLook(bool enabled) override;
    void setMouseSensitivity(float sensitivity) override { mouseLook_.setSensitivity(sensitivity); }
    void enableMouseAttack(bool enabled) override { mouseAttack_.setEnabled(enabled); }
    std::vector<Extent> displayModes() const override;
    bool setDisplay(const DisplayRequest& request) override;
    bool fullscreen() const override;
    void setKeyBindings(const KeyBindings& bindings) override { bindings_ = bindings; }
    KeyCode takePressedKey() override { return std::exchange(pressedKey_, KeyCode{0}); }

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
    bool overlayKey_ = false;
    bool helpKey_ = false;
    bool soundLegendKey_ = false;
    bool captionKey_ = false;
    bool noteKey_ = false;
    TypingGuard typing_;
    InputFrame rawInput_; // before the typing guard, so closing the box knows which keys are still down
    std::string typed_;
    bool focused_ = true;
    KeyBindings bindings_;
    KeyCode pressedKey_ = 0;
    bool arrowsReserved_ = false;
    bool lookEnabled_ = false;
    MouseLook mouseLook_;
    MouseAttack mouseAttack_;
    double lastPump_ = 0.0;
    std::function<void(const SDL_Event&)> hook_;
};

}
