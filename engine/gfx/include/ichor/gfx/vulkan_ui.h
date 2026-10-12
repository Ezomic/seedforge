#pragma once

#include "ichor/gfx/vk_context.h"

#include <RmlUi/Core/RenderInterface.h>

#include <cstdint>
#include <map>
#include <vector>

namespace ichor {

// Draws RmlUi geometry with Vulkan, inside a dynamic rendering pass that is
// already open (the overlay hook of the scene renderer). Between begin and end
// the viewport must be upright, since the vertex shader maps top-left pixels.
//
// Buffers and textures RmlUi releases may still be read by frames in flight,
// so they are destroyed a few frames later, not at once. The VulkanContext
// must outlive this object.
//
// Not yet supported: CSS transforms and clipping by rounded overflow, which
// need RmlUi's SetTransform and clip mask calls. Plain boxes, borders, rounded
// backgrounds, scissor clipping and textures work.
class VulkanUiRenderer final : public Rml::RenderInterface {
public:
    VulkanUiRenderer(VulkanContext& context, VkFormat colorFormat, VkFormat depthFormat);
    ~VulkanUiRenderer() override;
    VulkanUiRenderer(const VulkanUiRenderer&) = delete;
    VulkanUiRenderer& operator=(const VulkanUiRenderer&) = delete;

    bool init();

    void begin(VkCommandBuffer cmd, VkExtent2D extent);
    void end();

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices, Rml::Span<const int> indices) override;
    void RenderGeometry(Rml::CompiledGeometryHandle geometry, Rml::Vector2f translation, Rml::TextureHandle texture) override;
    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override;
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i dimensions) override;
    void ReleaseTexture(Rml::TextureHandle texture) override;
    void EnableScissorRegion(bool enable) override;
    void SetScissorRegion(Rml::Rectanglei region) override;

    std::size_t liveGeometry() const { return geometry_.size(); }
    std::size_t liveTextures() const { return textures_.size(); }

private:
    struct Geometry {
        GpuBuffer vertices;
        GpuBuffer indices;
        std::uint32_t indexCount = 0;
    };
    struct Texture {
        GpuImage image;
        VkDescriptorSet set = VK_NULL_HANDLE;
    };
    struct Retired {
        std::uint64_t frame = 0;
        Geometry geometry;
        Texture texture;
    };

    Rml::TextureHandle upload(const std::uint8_t* rgba, std::uint32_t width, std::uint32_t height);
    void destroyRetired(bool all);
    void applyScissor();

    VulkanContext& ctx_;
    VkFormat colorFormat_;
    VkFormat depthFormat_;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    Rml::TextureHandle white_ = 0;

    std::map<Rml::CompiledGeometryHandle, Geometry> geometry_;
    std::map<Rml::TextureHandle, Texture> textures_;
    std::vector<Retired> retired_;
    Rml::CompiledGeometryHandle nextGeometry_ = 1;
    Rml::TextureHandle nextTexture_ = 1;
    std::uint64_t frame_ = 0;

    VkCommandBuffer cmd_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    bool scissorOn_ = false;
    Rml::Rectanglei scissor_;
};

}
