#include "ichor/gfx/vulkan_renderer.h"

#include "ichor/assets/image.h"
#include "ichor/core/log.h"
#include "ichor/gfx/vk_context.h"
#include "ichor/gfx/vk_scene.h"
#include "ichor/gfx/vulkan_ui.h"
#include "ichor/platform/sdl_platform.h"

#include <SDL3/SDL.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <tracy/Tracy.hpp>

#include <array>
#include <cstring>
#include <utility>
#include <vector>

namespace ichor {

namespace {

void imguiCheck(VkResult result)
{
    if (result != VK_SUCCESS) {
        logf(LogLevel::Error, "imgui vulkan: VkResult {}", static_cast<int>(result));
    }
}

bool validationRequested()
{
    const char* env = SDL_getenv("ICHOR_VK_VALIDATION");
    return env != nullptr && env[0] == '1';
}

}

struct VulkanRenderer::Impl {
    SdlPlatform& platform;
    VulkanContext ctx;
    std::unique_ptr<SceneRenderer> scene;
    std::unique_ptr<VulkanUiRenderer> ui;

    vkb::Swapchain swapchain{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkSemaphore> renderFinished; // one per swapchain image
    GpuImage depth;

    std::array<VkCommandBuffer, kFrameSlots> commandBuffers{};
    std::array<VkSemaphore, kFrameSlots> imageAvailable{};
    std::array<VkFence, kFrameSlots> inFlight{};
    std::uint32_t frame = 0;
    std::uint32_t imageIndex = 0;
    bool frameOpen = false;

    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    bool imguiContext = false;
    bool imguiReady = false;

    explicit Impl(SdlPlatform& p)
        : platform(p)
    {
    }

    bool canScreenshot = false;
    bool createSwapchain();
    void destroySwapchain();
    bool recreateSwapchain();
    bool createCommands();
    bool createImgui();
    void destroyAll();
};

bool VulkanRenderer::Impl::createSwapchain()
{
    const Extent size = platform.drawableSize();
    if (size.width <= 0 || size.height <= 0) {
        return false;
    }
    // Screenshots copy out of the swapchain image, which needs the surface to allow it.
    VkSurfaceCapabilitiesKHR capabilities{};
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(ctx.physicalDevice(), ctx.surface(), &capabilities);
    canScreenshot = (capabilities.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    vkb::SwapchainBuilder builder{ctx.vkbDevice(), ctx.surface()};
    if (canScreenshot) {
        builder.add_image_usage_flags(VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
    }
    auto result = builder.set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                      .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                      .set_desired_extent(static_cast<std::uint32_t>(size.width), static_cast<std::uint32_t>(size.height))
                      .set_old_swapchain(swapchain)
                      .build();
    if (!result) {
        logf(LogLevel::Error, "vulkan swapchain: {}", result.error().message());
        return false;
    }
    if (result.value().image_format != VK_FORMAT_B8G8R8A8_UNORM && result.value().image_format != VK_FORMAT_R8G8B8A8_UNORM) {
        // The shaders gamma-encode themselves; an sRGB target would double it.
        logf(LogLevel::Error, "vulkan swapchain: no UNORM format offered (got {})", static_cast<int>(result.value().image_format));
        vkb::destroy_swapchain(result.value());
        return false;
    }
    destroySwapchain();
    vkb::destroy_swapchain(swapchain);
    swapchain = result.value();
    swapchainImages = swapchain.get_images().value();
    swapchainViews = swapchain.get_image_views().value();

    renderFinished.resize(swapchainImages.size());
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (auto& semaphore : renderFinished) {
        if (!vkCheck(vkCreateSemaphore(ctx.device(), &semaphoreInfo, nullptr, &semaphore), "vkCreateSemaphore")) {
            return false;
        }
    }
    depth = ctx.createImage(swapchain.extent, SceneRenderer::depthFormat(), VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);
    return depth.image != VK_NULL_HANDLE;
}

void VulkanRenderer::Impl::destroySwapchain()
{
    ctx.destroyImage(depth);
    for (auto semaphore : renderFinished) {
        vkDestroySemaphore(ctx.device(), semaphore, nullptr);
    }
    renderFinished.clear();
    swapchain.destroy_image_views(swapchainViews);
    swapchainViews.clear();
    swapchainImages.clear();
}

bool VulkanRenderer::Impl::recreateSwapchain()
{
    vkDeviceWaitIdle(ctx.device());
    return createSwapchain();
}

bool VulkanRenderer::Impl::createCommands()
{
    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = ctx.commandPool();
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFrameSlots;
    if (!vkCheck(vkAllocateCommandBuffers(ctx.device(), &allocInfo, commandBuffers.data()), "vkAllocateCommandBuffers")) {
        return false;
    }
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (std::uint32_t i = 0; i < kFrameSlots; ++i) {
        if (!vkCheck(vkCreateSemaphore(ctx.device(), &semaphoreInfo, nullptr, &imageAvailable[i]), "vkCreateSemaphore")
            || !vkCheck(vkCreateFence(ctx.device(), &fenceInfo, nullptr, &inFlight[i]), "vkCreateFence")) {
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::Impl::createImgui()
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().IniFilename = nullptr;
    ImGui_ImplSDL3_InitForVulkan(platform.window());
    imguiContext = true;

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 16};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 16;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    if (!vkCheck(vkCreateDescriptorPool(ctx.device(), &poolInfo, nullptr, &imguiPool), "vkCreateDescriptorPool")) {
        return false;
    }

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &swapchain.image_format;
    const VkFormat depthFormat = SceneRenderer::depthFormat();
    rendering.depthAttachmentFormat = depthFormat;

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = ctx.instance();
    info.PhysicalDevice = ctx.physicalDevice();
    info.Device = ctx.device();
    info.QueueFamily = ctx.queueFamily();
    info.Queue = ctx.queue();
    info.DescriptorPool = imguiPool;
    info.MinImageCount = kFrameSlots;
    info.ImageCount = static_cast<std::uint32_t>(swapchainImages.size());
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = rendering;
    info.UseDynamicRendering = true;
    info.CheckVkResultFn = imguiCheck;
    imguiReady = ImGui_ImplVulkan_Init(&info);
    return imguiReady;
}

void VulkanRenderer::Impl::destroyAll()
{
    if (ctx.device() == VK_NULL_HANDLE) {
        return;
    }
    vkDeviceWaitIdle(ctx.device());
    if (imguiReady) {
        ImGui_ImplVulkan_Shutdown();
    }
    if (imguiContext) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
    ui.reset();
    scene.reset();
    vkDestroyDescriptorPool(ctx.device(), imguiPool, nullptr);
    for (std::uint32_t i = 0; i < kFrameSlots; ++i) {
        vkDestroySemaphore(ctx.device(), imageAvailable[i], nullptr);
        vkDestroyFence(ctx.device(), inFlight[i], nullptr);
    }
    destroySwapchain();
    vkb::destroy_swapchain(swapchain);
}

VulkanRenderer::VulkanRenderer(SdlPlatform& platform)
    : impl_(std::make_unique<Impl>(platform))
{
}

VulkanRenderer::~VulkanRenderer()
{
    impl_->destroyAll();
}

bool VulkanRenderer::init()
{
    Impl& v = *impl_;
    if (!v.ctx.init(v.platform.window(), validationRequested()) || !v.createSwapchain() || !v.createCommands()) {
        return false;
    }
    v.scene = std::make_unique<SceneRenderer>(v.ctx, v.swapchain.image_format);
    if (!v.scene->init() || !v.createImgui()) {
        return false;
    }
    v.ui = std::make_unique<VulkanUiRenderer>(v.ctx, v.swapchain.image_format, SceneRenderer::depthFormat());
    if (!v.ui->init()) {
        return false;
    }
    v.platform.setEventHook([](const SDL_Event& event) { ImGui_ImplSDL3_ProcessEvent(&event); });
    return true;
}

Rml::RenderInterface* VulkanRenderer::uiRenderInterface()
{
    return impl_->ui.get();
}

bool VulkanRenderer::uploadBundle(const SceneBundle& bundle)
{
    return impl_->scene->uploadBundle(bundle);
}

std::uint32_t VulkanRenderer::meshCount() const
{
    return impl_->scene ? impl_->scene->meshCount() : 0;
}

bool VulkanRenderer::setTerrainMaterials(const SceneBundle& layers)
{
    return impl_->scene->setTerrainMaterials(layers);
}

std::uint32_t VulkanRenderer::uploadTerrainCell(const std::vector<Mesh>& lods)
{
    return impl_->scene->uploadTerrainCell(lods);
}

void VulkanRenderer::removeTerrainCell(std::uint32_t cell)
{
    impl_->scene->removeTerrainCell(cell);
}

std::uint32_t VulkanRenderer::uploadWaterCell(const Mesh& surface)
{
    return impl_->scene->uploadWaterCell(surface);
}

void VulkanRenderer::removeWaterCell(std::uint32_t cell)
{
    impl_->scene->removeWaterCell(cell);
}

bool VulkanRenderer::setVegetationMeshes(const SceneBundle& trees)
{
    return impl_->scene->setVegetationMeshes(trees);
}

std::uint32_t VulkanRenderer::uploadVegetationCell(const std::vector<PlantInstance>& plants)
{
    return impl_->scene->uploadVegetationCell(plants);
}

void VulkanRenderer::removeVegetationCell(std::uint32_t cell)
{
    impl_->scene->removeVegetationCell(cell);
}

bool VulkanRenderer::reloadShaders()
{
    return impl_->scene->reloadPipelines();
}

void VulkanRenderer::beginFrame()
{
    ZoneScoped;
    Impl& v = *impl_;
    lines_.clear();
    hasView_ = false;
    v.frameOpen = false;

    const Extent size = v.platform.drawableSize();
    if (size.width <= 0 || size.height <= 0) {
        SDL_Delay(16);
        return;
    }
    if (!vkCheck(vkWaitForFences(v.ctx.device(), 1, &v.inFlight[v.frame], VK_TRUE, UINT64_MAX), "vkWaitForFences")) {
        return;
    }

    const VkResult acquire = vkAcquireNextImageKHR(v.ctx.device(), v.swapchain.swapchain, UINT64_MAX,
        v.imageAvailable[v.frame], VK_NULL_HANDLE, &v.imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        if (!v.recreateSwapchain()) {
            log(LogLevel::Warn, "vulkan: swapchain recreation failed, skipping frame");
        }
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        vkCheck(acquire, "vkAcquireNextImageKHR");
        return;
    }
    vkResetFences(v.ctx.device(), 1, &v.inFlight[v.frame]);
    v.frameOpen = true;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void VulkanRenderer::submit(const RenderView& view)
{
    view_ = view;
    hasView_ = true;
}

void VulkanRenderer::debugText(std::string line)
{
    lines_.push_back(std::move(line));
}

void VulkanRenderer::endFrame()
{
    ZoneScoped;
    Impl& v = *impl_;
    if (!v.frameOpen) {
        return;
    }

    ImGui::SetNextWindowPos({12.0f, 12.0f}, ImGuiCond_Always);
    ImGui::Begin("debug", nullptr, ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs);
    for (const auto& line : lines_) {
        ImGui::TextUnformatted(line.c_str());
    }
    ImGui::End();
    if (guiHook_) {
        guiHook_();
    }
    ImGui::Render();

    const bool uiLive = v.ui != nullptr && uiRender_ && v.swapchain.extent.width > 0 && v.swapchain.extent.height > 0;
    if (uiLive && uiUpdate_) {
        uiUpdate_(v.swapchain.extent.width, v.swapchain.extent.height);
    }

    VkCommandBuffer cmd = v.commandBuffers[v.frame];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImage image = v.swapchainImages[v.imageIndex];
    transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);
    transitionImage(cmd, v.depth.image, VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT, 0,
        VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT, VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT);

    static const RenderView kEmptyView;
    const RenderView& view = hasView_ ? view_ : kEmptyView;
    v.scene->render(cmd, v.frame, view, v.swapchainViews[v.imageIndex], v.depth.view, v.swapchain.extent,
        [&](VkCommandBuffer overlayCmd) {
            // The game UI first, so the debug overlay and the editor stay on top of it.
            if (uiLive) {
                v.ui->begin(overlayCmd, v.swapchain.extent);
                uiRender_();
                v.ui->end();
            }
            ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), overlayCmd);
        });

    // A screenshot copies the finished image into a host buffer on its way to the display.
    GpuBuffer shot;
    const bool capturing = !screenshotPath_.empty() && v.canScreenshot;
    if (capturing) {
        shot = v.ctx.createBuffer(static_cast<VkDeviceSize>(v.swapchain.extent.width) * v.swapchain.extent.height * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VulkanContext::HostAccess::Read);
    }
    if (capturing && shot.mapped != nullptr) {
        transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT);
        VkBufferImageCopy region{};
        region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
        region.imageExtent = {v.swapchain.extent.width, v.swapchain.extent.height, 1};
        vkCmdCopyImageToBuffer(cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, shot.buffer, 1, &region);
        transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COPY_BIT, VK_ACCESS_2_TRANSFER_READ_BIT,
            VK_PIPELINE_STAGE_2_NONE, 0);
    } else {
        transitionImage(cmd, image, VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
            VK_PIPELINE_STAGE_2_NONE, 0);
    }
    vkEndCommandBuffer(cmd);

    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &v.imageAvailable[v.frame];
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &v.renderFinished[v.imageIndex];
    const bool submitted = vkCheck(vkQueueSubmit(v.ctx.queue(), 1, &submit, v.inFlight[v.frame]), "vkQueueSubmit");
    if (submitted && !screenshotPath_.empty()) {
        if (shot.mapped == nullptr) {
            log(LogLevel::Warn, "screenshot: the window cannot be captured here (no transfer usage on the swapchain, or no memory for it)");
        } else {
            vkWaitForFences(v.ctx.device(), 1, &v.inFlight[v.frame], VK_TRUE, UINT64_MAX);
            vmaInvalidateAllocation(v.ctx.allocator(), shot.allocation, 0, VK_WHOLE_SIZE);
            const std::uint32_t width = v.swapchain.extent.width;
            const std::uint32_t height = v.swapchain.extent.height;
            std::vector<std::uint8_t> rgba(static_cast<std::size_t>(width) * height * 4);
            std::memcpy(rgba.data(), shot.mapped, rgba.size());
            if (v.swapchain.image_format == VK_FORMAT_B8G8R8A8_UNORM) {
                for (std::size_t i = 0; i < rgba.size(); i += 4) {
                    std::swap(rgba[i], rgba[i + 2]);
                }
            }
            for (std::size_t i = 3; i < rgba.size(); i += 4) {
                rgba[i] = 255; // the window is opaque whatever the target's alpha holds
            }
            if (writePng(screenshotPath_, width, height, rgba.data())) {
                logf(LogLevel::Info, "screenshot: {}x{} saved to {}", width, height, screenshotPath_);
            } else {
                logf(LogLevel::Error, "screenshot: could not write {}", screenshotPath_);
            }
        }
    }
    if (shot.buffer != VK_NULL_HANDLE) {
        if (submitted) {
            vkWaitForFences(v.ctx.device(), 1, &v.inFlight[v.frame], VK_TRUE, UINT64_MAX);
        }
        v.ctx.destroyBuffer(shot);
    }
    screenshotPath_.clear();
    if (!submitted) {
        // The fence would never signal; put it back so the next wait returns.
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkDestroyFence(v.ctx.device(), v.inFlight[v.frame], nullptr);
        vkCheck(vkCreateFence(v.ctx.device(), &fenceInfo, nullptr, &v.inFlight[v.frame]), "vkCreateFence");
        v.frame = (v.frame + 1) % kFrameSlots;
        return;
    }

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &v.renderFinished[v.imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &v.swapchain.swapchain;
    present.pImageIndices = &v.imageIndex;
    const VkResult presented = vkQueuePresentKHR(v.ctx.queue(), &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        if (!v.recreateSwapchain()) {
            log(LogLevel::Warn, "vulkan: swapchain recreation failed after present");
        }
    } else {
        vkCheck(presented, "vkQueuePresentKHR");
    }

    v.frame = (v.frame + 1) % kFrameSlots;
    FrameMark;
}

}
