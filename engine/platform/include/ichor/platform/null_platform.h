#pragma once

#include "ichor/platform/platform.h"

namespace ichor {

// No window, no input, and a clock that advances one fixed tick per pump,
// so nothing headless can pick up wall time. The headless build and every
// test run on this.
class NullPlatform final : public Platform {
public:
    bool init(const WindowDesc& desc) override;
    void pumpEvents() override;
    bool quitRequested() const override { return quit_; }
    double now() const override;
    Extent drawableSize() const override { return {}; }

    void requestQuit() { quit_ = true; }
    unsigned pumpCount() const { return pumps_; }

    static constexpr double kTick = 1.0 / 60.0;

private:
    bool quit_ = false;
    unsigned pumps_ = 0;
};

}
