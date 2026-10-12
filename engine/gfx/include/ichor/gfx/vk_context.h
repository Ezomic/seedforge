#pragma once

#include <VkBootstrap.h>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <cstdint>
#include <functional>
#include <string_view>
#include <vector>

struct SDL_Window;

namespace ichor {

struct Texture;

struct GpuBuffer {
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkDeviceSize size = 0;
    void* mapped = nullptr;
};

struct GpuImage {
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent{};
    std::uint32_t mipLevels = 1;
    std::uint32_t layers = 1;
};

bool vkCheck(VkResult result, const char* what);

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
    VkImageLayout from, VkImageLayout to,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
    std::uint32_t mipLevels = 1, std::uint32_t layers = 1);

// Instance, device, queue, allocator and a command pool. With a window it also
// owns the surface; without one it is a headless device for offscreen work.
class VulkanContext {
public:
    VulkanContext() = default;
    ~VulkanContext();
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;

    bool init(SDL_Window* window, bool validation);

    VkDevice device() const { return device_.device; }
    VkPhysicalDevice physicalDevice() const { return device_.physical_device; }
    VkInstance instance() const { return instance_.instance; }
    VkSurfaceKHR surface() const { return surface_; }
    VkQueue queue() const { return queue_; }
    std::uint32_t queueFamily() const { return family_; }
    VmaAllocator allocator() const { return allocator_; }
    VkCommandPool commandPool() const { return pool_; }
    const vkb::Device& vkbDevice() const { return device_; }
    const char* deviceName() const { return device_.physical_device.name.c_str(); }

    GpuBuffer createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, bool hostVisible);
    void destroyBuffer(GpuBuffer& buffer);
    bool uploadBuffer(GpuBuffer& dst, const void* data, VkDeviceSize size);

    GpuImage createImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
        std::uint32_t mipLevels = 1, std::uint32_t layers = 1);
    VkImageView createLayerView(const GpuImage& image, VkImageAspectFlags aspect, std::uint32_t layer);
    void destroyImage(GpuImage& image);
    bool uploadTexture(GpuImage& dst, const Texture& texture);

    bool immediate(const std::function<void(VkCommandBuffer)>& record);
    VkShaderModule loadShader(std::string_view name);

private:
    vkb::Instance instance_{};
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    vkb::Device device_{};
    VkQueue queue_ = VK_NULL_HANDLE;
    std::uint32_t family_ = 0;
    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkFence immediateFence_ = VK_NULL_HANDLE;
};

}
