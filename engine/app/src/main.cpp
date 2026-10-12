#include "ichor/loop/engine.h"
#include "ichor/core/log.h"
#include "ichor/gfx/null_renderer.h"
#include "ichor/gfx/vulkan_renderer.h"
#include "ichor/platform/null_platform.h"
#include "ichor/platform/sdl_platform.h"

#include <tracy/Tracy.hpp>

#include <charconv>
#include <cstdint>
#include <memory>
#include <string_view>

namespace {

struct Options {
    bool headless = false;
    std::uint64_t frames = 0; // 0 runs until quit
    std::uint64_t seed = 1;
};

template <class T>
bool parseNumber(std::string_view text, T& out)
{
    const auto result = std::from_chars(text.data(), text.data() + text.size(), out);
    return result.ec == std::errc{} && result.ptr == text.data() + text.size();
}

bool parse(int argc, char** argv, Options& options)
{
    for (int i = 1; i < argc; ++i) {
        const std::string_view arg = argv[i];
        const bool hasValue = i + 1 < argc;
        if (arg == "--headless") {
            options.headless = true;
        } else if (arg == "--frames" && hasValue) {
            if (!parseNumber(argv[++i], options.frames)) return false;
        } else if (arg == "--seed" && hasValue) {
            if (!parseNumber(argv[++i], options.seed)) return false;
        } else {
            ichor::logf(ichor::LogLevel::Error, "unknown argument: {}", arg);
            return false;
        }
    }
    return true;
}

int run(ichor::Platform& platform, ichor::Renderer& renderer, const Options& options)
{
    if (!platform.init({}) || !renderer.init()) {
        return 1;
    }

    ichor::EngineConfig config;
    config.seed = options.seed;
    ichor::Engine engine(platform, renderer, config);

    double last = platform.now();
    for (;;) {
        ZoneScopedN("frame");
        const double now = platform.now();
        const double dt = options.headless ? config.fixedDt : now - last;
        last = now;
        if (!engine.frame(dt)) {
            break;
        }
        if (options.frames != 0 && engine.frameCount() >= options.frames) {
            break;
        }
    }

    ichor::logf(ichor::LogLevel::Info, "exit after {} frames, {} ticks, state {:016x}",
        engine.frameCount(), engine.scene().tickCount(), engine.scene().stateHash());
    return 0;
}

}

int main(int argc, char** argv)
{
    Options options;
    if (!parse(argc, argv, options)) {
        ichor::log(ichor::LogLevel::Info, "usage: ichor [--headless] [--frames N] [--seed S]");
        return 2;
    }

    if (options.headless) {
        ichor::NullPlatform platform;
        ichor::NullRenderer renderer;
        return run(platform, renderer, options);
    }

    ichor::SdlPlatform platform;
    ichor::VulkanRenderer renderer(platform);
    return run(platform, renderer, options);
}
