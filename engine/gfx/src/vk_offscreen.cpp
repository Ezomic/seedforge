#include "ichor/gfx/vk_offscreen.h"

#include "ichor/core/log.h"

#include <cstring>

namespace ichor {

OffscreenRenderer::OffscreenRenderer() = default;

OffscreenRenderer::~OffscreenRenderer()
{
    if (context_.device() == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(context_.device());
    scene_.reset();
    context_.destroyBuffer(readback_);
    context_.destroyImage(color_);
    context_.destroyImage(depth_);
}

bool OffscreenRenderer::init(std::uint32_t width, std::uint32_t height, bool validation)
{
    if (!context_.init(nullptr, validation)) {
        return false;
    }
    extent_ = {width, height};
    color_ = context_.createImage(extent_, VK_FORMAT_R8G8B8A8_UNORM,
        VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT, VK_IMAGE_ASPECT_COLOR_BIT);
    depth_ = context_.createImage(extent_, SceneRenderer::depthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    readback_ = context_.createBuffer(static_cast<VkDeviceSize>(width) * height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanContext::HostAccess::Read);
    if (color_.image == VK_NULL_HANDLE || depth_.image == VK_NULL_HANDLE || readback_.mapped == nullptr) {
        return false;
    }
    scene_ = std::make_unique<SceneRenderer>(context_, VK_FORMAT_R8G8B8A8_UNORM);
    return scene_->init();
}

bool OffscreenRenderer::uploadBundle(const SceneBundle& bundle)
{
    return scene_->uploadBundle(bundle);
}

bool OffscreenRenderer::render(const RenderView& view, std::vector<std::uint8_t>& rgba, const Overlay& overlay)
{
    const bool ok = context_.immediate([&](VkCommandBuffer cmd) {
        transitionImage(cmd, color_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
        transitionImage(cmd, depth_.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

        std::function<void(VkCommandBuffer)> record;
        if (overlay) {
            record = [&](VkCommandBuffer overlayCmd) { overlay(overlayCmd, extent_); };
        }
        scene_->render(cmd, 0, view, color_.view, depth_.view, extent_, record);

        transitionImage(cmd, color_.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {extent_.width, extent_.height, 1};
        vkCmdCopyImageToBuffer(cmd, color_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback_.buffer, 1, &region);

        VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
        barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
        barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
        barrier.dstStageMask = VK_PIPELINE_STAGE_2_HOST_BIT;
        barrier.dstAccessMask = VK_ACCESS_2_HOST_READ_BIT;
        VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
        dependency.memoryBarrierCount = 1;
        dependency.pMemoryBarriers = &barrier;
        vkCmdPipelineBarrier2(cmd, &dependency);
    });
    if (!ok) {
        return false;
    }
    vmaInvalidateAllocation(context_.allocator(), readback_.allocation, 0, VK_WHOLE_SIZE);
    rgba.resize(static_cast<std::size_t>(extent_.width) * extent_.height * 4);
    std::memcpy(rgba.data(), readback_.mapped, rgba.size());
    return true;
}

}
