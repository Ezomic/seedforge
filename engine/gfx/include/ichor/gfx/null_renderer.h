#pragma once

#include "ichor/gfx/renderer.h"

#include <cstdint>

namespace ichor {

class NullRenderer final : public Renderer {
public:
    bool init() override { return true; }
    bool uploadBundle(const SceneBundle& bundle) override;
    std::uint32_t meshCount() const override { return meshes_; }
    void beginFrame() override;
    void submit(const RenderView& view) override;
    void endFrame() override;
    void debugText(std::string line) override;

    std::uint64_t framesBegun() const { return begun_; }
    std::uint64_t framesEnded() const { return ended_; }
    std::uint64_t linesDropped() const { return lines_; }
    std::uint64_t instancesSubmitted() const { return instances_; }
    std::uint64_t skinnedInstancesSubmitted() const { return skinned_; }
    std::uint32_t meshesUploaded() const { return meshes_; }

private:
    std::uint64_t begun_ = 0;
    std::uint64_t ended_ = 0;
    std::uint64_t lines_ = 0;
    std::uint64_t instances_ = 0;
    std::uint64_t skinned_ = 0;
    std::uint32_t meshes_ = 0;
};

}
