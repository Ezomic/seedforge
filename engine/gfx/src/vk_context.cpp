#include "ichor/gfx/vk_context.h"

#include "ichor/assets/bundle.h"
#include "ichor/assets/texture_cook.h"
#include "ichor/core/log.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>

namespace ichor {

namespace {
std::filesystem::path g_shaderDirectory = ICHOR_SHADER_DIR;
bool g_blockCompressionAllowed = true;
}

void setShaderDirectory(std::filesystem::path directory)
{
    g_shaderDirectory = std::move(directory);
}

const std::filesystem::path& shaderDirectory()
{
    return g_shaderDirectory;
}

void setBlockCompressionAllowed(bool allowed)
{
    g_blockCompressionAllowed = allowed;
}

bool vkCheck(VkResult result, const char* what)
{
    if (result != VK_SUCCESS) {
        logf(LogLevel::Error, "{} failed: VkResult {}", what, static_cast<int>(result));
        return false;
    }
    return true;
}

void transitionImage(VkCommandBuffer cmd, VkImage image, VkImageAspectFlags aspect,
    VkImageLayout from, VkImageLayout to,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess,
    std::uint32_t mipLevels, std::uint32_t layers)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.image = image;
    barrier.subresourceRange = {aspect, 0, mipLevels, 0, layers};

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
}

VulkanContext::~VulkanContext()
{
    if (device_.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_.device);
        vkDestroyFence(device_.device, immediateFence_, nullptr);
        vkDestroyCommandPool(device_.device, pool_, nullptr);
        vmaDestroyAllocator(allocator_);
        vkb::destroy_device(device_);
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkb::destroy_surface(instance_, surface_);
    }
    vkb::destroy_instance(instance_);
}

bool VulkanContext::init(SDL_Window* window, bool validation)
{
    const bool headless = window == nullptr;

    vkb::InstanceBuilder instanceBuilder;
    instanceBuilder.set_app_name("Ichor").require_api_version(1, 3, 0).set_headless(headless);
    if (validation) {
        instanceBuilder.request_validation_layers(true).use_default_debug_messenger();
    }
    if (!headless) {
        std::uint32_t extensionCount = 0;
        const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
        for (std::uint32_t i = 0; i < extensionCount; ++i) {
            instanceBuilder.enable_extension(extensions[i]);
        }
    }
    auto instanceResult = instanceBuilder.build();
    if (!instanceResult) {
        logf(LogLevel::Error, "vulkan instance: {}", instanceResult.error().message());
        return false;
    }
    instance_ = instanceResult.value();

    if (!headless && !SDL_Vulkan_CreateSurface(window, instance_.instance, nullptr, &surface_)) {
        logf(LogLevel::Error, "SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    VkPhysicalDeviceFeatures features{};
    features.samplerAnisotropy = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{instance_};
    selector.set_minimum_version(1, 3).set_required_features_13(features13).set_required_features(features);
    if (headless) {
        selector.require_present(false);
    } else {
        selector.set_surface(surface_);
    }
    auto physicalResult = selector.select();
    if (!physicalResult) {
        logf(LogLevel::Error, "vulkan physical device: {}", physicalResult.error().message());
        return false;
    }

    // BC7, BC5 and BC4 are optional: without them the renderer decodes compressed textures to RGBA8 before upload.
    VkPhysicalDeviceFeatures optionalFeatures{};
    optionalFeatures.textureCompressionBC = VK_TRUE;
    blockCompression_ = physicalResult.value().enable_features_if_present(optionalFeatures);
    if (const char* off = SDL_getenv("ICHOR_NO_BC"); !g_blockCompressionAllowed || (off != nullptr && off[0] == '1')) {
        blockCompression_ = false;
    }

    auto deviceResult = vkb::DeviceBuilder{physicalResult.value()}.build();
    if (!deviceResult) {
        logf(LogLevel::Error, "vulkan device: {}", deviceResult.error().message());
        return false;
    }
    device_ = deviceResult.value();
    logf(LogLevel::Info, "vulkan device: {}{}, block-compressed textures {}", device_.physical_device.name, headless ? " (headless)" : "",
        blockCompression_ ? "sampled directly" : "decoded to RGBA8");

    auto queueResult = device_.get_queue(vkb::QueueType::graphics);
    auto familyResult = device_.get_queue_index(vkb::QueueType::graphics);
    if (!queueResult || !familyResult) {
        log(LogLevel::Error, "vulkan: no graphics queue");
        return false;
    }
    queue_ = queueResult.value();
    family_ = familyResult.value();

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = device_.physical_device;
    allocatorInfo.device = device_.device;
    allocatorInfo.instance = instance_.instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    if (!vkCheck(vmaCreateAllocator(&allocatorInfo, &allocator_), "vmaCreateAllocator")) {
        return false;
    }

    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = family_;
    if (!vkCheck(vkCreateCommandPool(device_.device, &poolInfo, nullptr, &pool_), "vkCreateCommandPool")) {
        return false;
    }
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    return vkCheck(vkCreateFence(device_.device, &fenceInfo, nullptr, &immediateFence_), "vkCreateFence");
}

GpuBuffer VulkanContext::createBuffer(VkDeviceSize size, VkBufferUsageFlags usage, HostAccess access)
{
    GpuBuffer buffer;
    buffer.size = size;
    VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    info.size = size;
    info.usage = usage;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    if (access == HostAccess::Write) {
        alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    } else if (access == HostAccess::Read) {
        alloc.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
    }
    VmaAllocationInfo result{};
    if (!vkCheck(vmaCreateBuffer(allocator_, &info, &alloc, &buffer.buffer, &buffer.allocation, &result), "vmaCreateBuffer")) {
        return {};
    }
    buffer.mapped = result.pMappedData;
    return buffer;
}

void VulkanContext::destroyBuffer(GpuBuffer& buffer)
{
    if (buffer.buffer != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffer.buffer, buffer.allocation);
    }
    buffer = {};
}

bool VulkanContext::uploadBuffer(GpuBuffer& dst, const void* data, VkDeviceSize size)
{
    GpuBuffer staging = createBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, HostAccess::Write);
    if (staging.mapped == nullptr) {
        return false;
    }
    std::memcpy(staging.mapped, data, static_cast<std::size_t>(size));
    vmaFlushAllocation(allocator_, staging.allocation, 0, size);
    const bool ok = immediate([&](VkCommandBuffer cmd) {
        VkBufferCopy region{0, 0, size};
        vkCmdCopyBuffer(cmd, staging.buffer, dst.buffer, 1, &region);
    });
    destroyBuffer(staging);
    return ok;
}

GpuImage VulkanContext::createImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect,
    std::uint32_t mipLevels, std::uint32_t layers, VkComponentMapping swizzle)
{
    GpuImage image;
    image.format = format;
    image.extent = extent;
    image.mipLevels = mipLevels;
    image.layers = layers;

    VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = {extent.width, extent.height, 1};
    info.mipLevels = mipLevels;
    info.arrayLayers = layers;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VmaAllocationCreateInfo alloc{};
    alloc.usage = VMA_MEMORY_USAGE_AUTO;
    if (!vkCheck(vmaCreateImage(allocator_, &info, &alloc, &image.image, &image.allocation, nullptr), "vmaCreateImage")) {
        return {};
    }

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image.image;
    viewInfo.viewType = layers > 1 ? VK_IMAGE_VIEW_TYPE_2D_ARRAY : VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = format;
    viewInfo.components = swizzle;
    viewInfo.subresourceRange = {aspect, 0, mipLevels, 0, layers};
    if (!vkCheck(vkCreateImageView(device_.device, &viewInfo, nullptr, &image.view), "vkCreateImageView")) {
        destroyImage(image);
        return {};
    }
    return image;
}

VkImageView VulkanContext::createLayerView(const GpuImage& image, VkImageAspectFlags aspect, std::uint32_t layer)
{
    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.image = image.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = image.format;
    viewInfo.subresourceRange = {aspect, 0, 1, layer, 1};
    VkImageView view = VK_NULL_HANDLE;
    vkCheck(vkCreateImageView(device_.device, &viewInfo, nullptr, &view), "vkCreateImageView");
    return view;
}

void VulkanContext::destroyImage(GpuImage& image)
{
    if (image.view != VK_NULL_HANDLE) {
        vkDestroyImageView(device_.device, image.view, nullptr);
    }
    if (image.image != VK_NULL_HANDLE) {
        vmaDestroyImage(allocator_, image.image, image.allocation);
    }
    image = {};
}

bool VulkanContext::uploadTexture(GpuImage& dst, const Texture& texture)
{
    GpuBuffer staging = createBuffer(texture.pixels.size(), VK_BUFFER_USAGE_TRANSFER_SRC_BIT, HostAccess::Write);
    if (staging.mapped == nullptr) {
        return false;
    }
    std::memcpy(staging.mapped, texture.pixels.data(), texture.pixels.size());
    vmaFlushAllocation(allocator_, staging.allocation, 0, texture.pixels.size());

    std::vector<VkBufferImageCopy> regions;
    VkDeviceSize offset = 0;
    std::uint32_t w = texture.width;
    std::uint32_t h = texture.height;
    for (std::uint32_t mip = 0; mip < texture.mipCount; ++mip) {
        VkBufferImageCopy region{};
        region.bufferOffset = offset;
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, mip, 0, 1};
        region.imageExtent = {w, h, 1};
        regions.push_back(region);
        offset += mipByteSize(texture.format, w, h);
        w = std::max(1u, w / 2);
        h = std::max(1u, h / 2);
    }

    const bool ok = immediate([&](VkCommandBuffer cmd) {
        transitionImage(cmd, dst.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            VK_PIPELINE_STAGE_2_NONE, 0, VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, dst.mipLevels);
        vkCmdCopyBufferToImage(cmd, staging.buffer, dst.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            static_cast<std::uint32_t>(regions.size()), regions.data());
        transitionImage(cmd, dst.image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT, VK_ACCESS_2_SHADER_SAMPLED_READ_BIT, dst.mipLevels);
    });
    destroyBuffer(staging);
    return ok;
}

bool VulkanContext::createTexture(GpuImage& dst, const Texture& source)
{
    Texture decoded;
    const Texture* texture = textureForDevice(source, blockCompression_, decoded);
    if (texture == nullptr) {
        return false;
    }
    VkFormat format = VK_FORMAT_R8G8B8A8_UNORM;
    VkComponentMapping swizzle{};
    switch (texture->format) {
    case TextureFormat::Rgba8Srgb: format = VK_FORMAT_R8G8B8A8_SRGB; break;
    case TextureFormat::Bc7Srgb: format = VK_FORMAT_BC7_SRGB_BLOCK; break;
    case TextureFormat::Bc7Unorm: format = VK_FORMAT_BC7_UNORM_BLOCK; break;
    case TextureFormat::Bc5Unorm: format = VK_FORMAT_BC5_UNORM_BLOCK; break;
    case TextureFormat::Bc4Unorm:
        format = VK_FORMAT_BC4_UNORM_BLOCK;
        swizzle = {VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_R, VK_COMPONENT_SWIZZLE_ONE};
        break;
    default: break;
    }
    dst = createImage({texture->width, texture->height}, format, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT, VK_IMAGE_ASPECT_COLOR_BIT,
        texture->mipCount, 1, swizzle);
    return dst.image != VK_NULL_HANDLE && uploadTexture(dst, *texture);
}

bool VulkanContext::immediate(const std::function<void(VkCommandBuffer)>& record)
{
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = pool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    if (!vkCheck(vkAllocateCommandBuffers(device_.device, &allocInfo, &cmd), "vkAllocateCommandBuffers")) {
        return false;
    }
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);
    record(cmd);
    vkEndCommandBuffer(cmd);

    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    bool ok = vkCheck(vkResetFences(device_.device, 1, &immediateFence_), "vkResetFences")
        && vkCheck(vkQueueSubmit(queue_, 1, &submit, immediateFence_), "vkQueueSubmit")
        && vkCheck(vkWaitForFences(device_.device, 1, &immediateFence_, VK_TRUE, UINT64_MAX), "vkWaitForFences");
    vkFreeCommandBuffers(device_.device, pool_, 1, &cmd);
    return ok;
}

VkShaderModule VulkanContext::loadShader(std::string_view name)
{
    const std::filesystem::path path = shaderDirectory() / std::string(name);
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        logf(LogLevel::Error, "cannot open shader {}", path.string());
        return VK_NULL_HANDLE;
    }
    const auto size = static_cast<std::size_t>(in.tellg());
    if (size % 4 != 0) {
        logf(LogLevel::Error, "shader {} is not SPIR-V", path.string());
        return VK_NULL_HANDLE;
    }
    std::vector<std::uint32_t> words(size / 4);
    in.seekg(0);
    in.read(reinterpret_cast<char*>(words.data()), static_cast<std::streamsize>(size));

    VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    info.codeSize = size;
    info.pCode = words.data();
    VkShaderModule module = VK_NULL_HANDLE;
    vkCheck(vkCreateShaderModule(device_.device, &info, nullptr, &module), "vkCreateShaderModule");
    return module;
}

}
