#include "ichor/gfx/vulkan_renderer.h"

#include "ichor/core/log.h"
#include "ichor/platform/sdl_platform.h"

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>
#include <VkBootstrap.h>
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#include <tracy/Tracy.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.h>

#include <array>
#include <filesystem>
#include <fstream>
#include <vector>

namespace ichor {

namespace {

constexpr std::uint32_t kFramesInFlight = 2;

std::vector<char> readFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary | std::ios::ate);
    if (!in) {
        logf(LogLevel::Error, "cannot open {}", path.string());
        return {};
    }
    const auto size = static_cast<std::size_t>(in.tellg());
    std::vector<char> bytes(size);
    in.seekg(0);
    in.read(bytes.data(), static_cast<std::streamsize>(size));
    return bytes;
}

bool check(VkResult result, const char* what)
{
    if (result != VK_SUCCESS) {
        logf(LogLevel::Error, "{} failed: VkResult {}", what, static_cast<int>(result));
        return false;
    }
    return true;
}

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

    vkb::Instance instance{};
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    vkb::Device device{};
    VkQueue graphicsQueue = VK_NULL_HANDLE;
    std::uint32_t graphicsFamily = 0;
    VmaAllocator allocator = VK_NULL_HANDLE;

    vkb::Swapchain swapchain{};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainViews;
    std::vector<VkSemaphore> renderFinished; // one per swapchain image

    VkCommandPool commandPool = VK_NULL_HANDLE;
    std::array<VkCommandBuffer, kFramesInFlight> commandBuffers{};
    std::array<VkSemaphore, kFramesInFlight> imageAvailable{};
    std::array<VkFence, kFramesInFlight> inFlight{};
    std::uint32_t frame = 0;
    std::uint32_t imageIndex = 0;
    bool frameOpen = false;

    VkPipelineLayout pipelineLayout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool imguiPool = VK_NULL_HANDLE;
    bool imguiContext = false;
    bool imguiReady = false;

    explicit Impl(SdlPlatform& p)
        : platform(p)
    {
    }

    bool createInstanceAndDevice();
    bool createSwapchain();
    void destroySwapchain();
    bool recreateSwapchain();
    bool createCommands();
    bool createPipeline();
    bool createImgui();
    void destroyAll();

    void transition(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
        VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
        VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess);
};

bool VulkanRenderer::Impl::createInstanceAndDevice()
{
    vkb::InstanceBuilder instanceBuilder;
    instanceBuilder.set_app_name("Ichor").require_api_version(1, 3, 0);
    if (validationRequested()) {
        instanceBuilder.request_validation_layers(true).use_default_debug_messenger();
    }

    std::uint32_t extensionCount = 0;
    const char* const* extensions = SDL_Vulkan_GetInstanceExtensions(&extensionCount);
    for (std::uint32_t i = 0; i < extensionCount; ++i) {
        instanceBuilder.enable_extension(extensions[i]);
    }

    auto instanceResult = instanceBuilder.build();
    if (!instanceResult) {
        logf(LogLevel::Error, "vulkan instance: {}", instanceResult.error().message());
        return false;
    }
    instance = instanceResult.value();

    if (!SDL_Vulkan_CreateSurface(platform.window(), instance.instance, nullptr, &surface)) {
        logf(LogLevel::Error, "SDL_Vulkan_CreateSurface failed: {}", SDL_GetError());
        return false;
    }

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;

    vkb::PhysicalDeviceSelector selector{instance};
    auto physicalResult = selector.set_surface(surface)
                              .set_minimum_version(1, 3)
                              .set_required_features_13(features13)
                              .select();
    if (!physicalResult) {
        logf(LogLevel::Error, "vulkan physical device: {}", physicalResult.error().message());
        return false;
    }

    auto deviceResult = vkb::DeviceBuilder{physicalResult.value()}.build();
    if (!deviceResult) {
        logf(LogLevel::Error, "vulkan device: {}", deviceResult.error().message());
        return false;
    }
    device = deviceResult.value();
    logf(LogLevel::Info, "vulkan device: {}", device.physical_device.name);

    auto queueResult = device.get_queue(vkb::QueueType::graphics);
    auto familyResult = device.get_queue_index(vkb::QueueType::graphics);
    if (!queueResult || !familyResult) {
        log(LogLevel::Error, "vulkan: no graphics queue");
        return false;
    }
    graphicsQueue = queueResult.value();
    graphicsFamily = familyResult.value();

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.physicalDevice = device.physical_device;
    allocatorInfo.device = device.device;
    allocatorInfo.instance = instance.instance;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    return check(vmaCreateAllocator(&allocatorInfo, &allocator), "vmaCreateAllocator");
}

bool VulkanRenderer::Impl::createSwapchain()
{
    const Extent size = platform.drawableSize();
    if (size.width <= 0 || size.height <= 0) {
        return false;
    }
    vkb::SwapchainBuilder builder{device};
    auto result = builder.set_desired_format({VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR})
                      .set_desired_present_mode(VK_PRESENT_MODE_FIFO_KHR)
                      .set_desired_extent(static_cast<std::uint32_t>(size.width), static_cast<std::uint32_t>(size.height))
                      .set_old_swapchain(swapchain)
                      .build();
    if (!result) {
        logf(LogLevel::Error, "vulkan swapchain: {}", result.error().message());
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
        if (!check(vkCreateSemaphore(device.device, &semaphoreInfo, nullptr, &semaphore), "vkCreateSemaphore")) {
            return false;
        }
    }
    return true;
}

void VulkanRenderer::Impl::destroySwapchain()
{
    for (auto semaphore : renderFinished) {
        vkDestroySemaphore(device.device, semaphore, nullptr);
    }
    renderFinished.clear();
    swapchain.destroy_image_views(swapchainViews);
    swapchainViews.clear();
    swapchainImages.clear();
}

bool VulkanRenderer::Impl::recreateSwapchain()
{
    vkDeviceWaitIdle(device.device);
    return createSwapchain();
}

bool VulkanRenderer::Impl::createCommands()
{
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsFamily;
    if (!check(vkCreateCommandPool(device.device, &poolInfo, nullptr, &commandPool), "vkCreateCommandPool")) {
        return false;
    }

    VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocInfo.commandPool = commandPool;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = kFramesInFlight;
    if (!check(vkAllocateCommandBuffers(device.device, &allocInfo, commandBuffers.data()), "vkAllocateCommandBuffers")) {
        return false;
    }

    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
        if (!check(vkCreateSemaphore(device.device, &semaphoreInfo, nullptr, &imageAvailable[i]), "vkCreateSemaphore")
            || !check(vkCreateFence(device.device, &fenceInfo, nullptr, &inFlight[i]), "vkCreateFence")) {
            return false;
        }
    }
    return true;
}

bool VulkanRenderer::Impl::createPipeline()
{
    const std::filesystem::path shaderDir = ICHOR_SHADER_DIR;
    const auto vertCode = readFile(shaderDir / "triangle.vert.spv");
    const auto fragCode = readFile(shaderDir / "triangle.frag.spv");
    if (vertCode.empty() || fragCode.empty()) {
        return false;
    }

    auto makeModule = [&](const std::vector<char>& code, VkShaderModule& module) {
        VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        info.codeSize = code.size();
        info.pCode = reinterpret_cast<const std::uint32_t*>(code.data());
        return check(vkCreateShaderModule(device.device, &info, nullptr, &module), "vkCreateShaderModule");
    };
    VkShaderModule vert = VK_NULL_HANDLE;
    VkShaderModule frag = VK_NULL_HANDLE;
    const auto destroyModules = [&] {
        vkDestroyShaderModule(device.device, vert, nullptr);
        vkDestroyShaderModule(device.device, frag, nullptr);
    };
    if (!makeModule(vertCode, vert) || !makeModule(fragCode, frag)) {
        destroyModules();
        return false;
    }

    VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    if (!check(vkCreatePipelineLayout(device.device, &layoutInfo, nullptr, &pipelineLayout), "vkCreatePipelineLayout")) {
        destroyModules();
        return false;
    }

    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vert;
    stages[0].pName = "main";
    stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = frag;
    stages[1].pName = "main";

    VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    VkPipelineRasterizationStateCreateInfo raster{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    VkPipelineColorBlendAttachmentState blendAttachment{};
    blendAttachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    blend.attachmentCount = 1;
    blend.pAttachments = &blendAttachment;
    const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &swapchain.image_format;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.pNext = &rendering;
    pipelineInfo.stageCount = static_cast<std::uint32_t>(stages.size());
    pipelineInfo.pStages = stages.data();
    pipelineInfo.pVertexInputState = &vertexInput;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewport;
    pipelineInfo.pRasterizationState = &raster;
    pipelineInfo.pMultisampleState = &multisample;
    pipelineInfo.pColorBlendState = &blend;
    pipelineInfo.pDynamicState = &dynamic;
    pipelineInfo.layout = pipelineLayout;

    const bool ok = check(vkCreateGraphicsPipelines(device.device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline), "vkCreateGraphicsPipelines");
    destroyModules();
    return ok;
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
    if (!check(vkCreateDescriptorPool(device.device, &poolInfo, nullptr, &imguiPool), "vkCreateDescriptorPool")) {
        return false;
    }

    VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachmentFormats = &swapchain.image_format;

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = instance.instance;
    info.PhysicalDevice = device.physical_device;
    info.Device = device.device;
    info.QueueFamily = graphicsFamily;
    info.Queue = graphicsQueue;
    info.DescriptorPool = imguiPool;
    info.MinImageCount = kFramesInFlight;
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
    if (device.device != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device.device);
    }
    if (imguiReady) {
        ImGui_ImplVulkan_Shutdown();
    }
    if (imguiContext) {
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
    }
    if (device.device != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device.device, imguiPool, nullptr);
        vkDestroyPipeline(device.device, pipeline, nullptr);
        vkDestroyPipelineLayout(device.device, pipelineLayout, nullptr);
        for (std::uint32_t i = 0; i < kFramesInFlight; ++i) {
            vkDestroySemaphore(device.device, imageAvailable[i], nullptr);
            vkDestroyFence(device.device, inFlight[i], nullptr);
        }
        vkDestroyCommandPool(device.device, commandPool, nullptr);
        destroySwapchain();
        vkb::destroy_swapchain(swapchain);
        vmaDestroyAllocator(allocator);
        vkb::destroy_device(device);
    }
    if (surface != VK_NULL_HANDLE) {
        vkb::destroy_surface(instance, surface);
    }
    vkb::destroy_instance(instance);
}

void VulkanRenderer::Impl::transition(VkCommandBuffer cmd, VkImage image, VkImageLayout from, VkImageLayout to,
    VkPipelineStageFlags2 srcStage, VkAccessFlags2 srcAccess,
    VkPipelineStageFlags2 dstStage, VkAccessFlags2 dstAccess)
{
    VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
    barrier.srcStageMask = srcStage;
    barrier.srcAccessMask = srcAccess;
    barrier.dstStageMask = dstStage;
    barrier.dstAccessMask = dstAccess;
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};

    VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(cmd, &dependency);
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
    if (!impl_->createInstanceAndDevice() || !impl_->createSwapchain() || !impl_->createCommands()
        || !impl_->createPipeline() || !impl_->createImgui()) {
        return false;
    }
    impl_->platform.setEventHook([](const SDL_Event& event) { ImGui_ImplSDL3_ProcessEvent(&event); });
    return true;
}

void VulkanRenderer::beginFrame()
{
    ZoneScoped;
    Impl& v = *impl_;
    lines_.clear();
    v.frameOpen = false;

    const Extent size = v.platform.drawableSize();
    if (size.width <= 0 || size.height <= 0) {
        SDL_Delay(16);
        return;
    }

    if (!check(vkWaitForFences(v.device.device, 1, &v.inFlight[v.frame], VK_TRUE, UINT64_MAX), "vkWaitForFences")) {
        return;
    }

    const VkResult acquire = vkAcquireNextImageKHR(v.device.device, v.swapchain.swapchain, UINT64_MAX,
        v.imageAvailable[v.frame], VK_NULL_HANDLE, &v.imageIndex);
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) {
        if (!v.recreateSwapchain()) {
            log(LogLevel::Warn, "vulkan: swapchain recreation failed, skipping frame");
        }
        return;
    }
    if (acquire != VK_SUCCESS && acquire != VK_SUBOPTIMAL_KHR) {
        check(acquire, "vkAcquireNextImageKHR");
        return;
    }
    vkResetFences(v.device.device, 1, &v.inFlight[v.frame]);
    v.frameOpen = true;

    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
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
    ImGui::Render();

    VkCommandBuffer cmd = v.commandBuffers[v.frame];
    vkResetCommandBuffer(cmd, 0);
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(cmd, &beginInfo);

    VkImage image = v.swapchainImages[v.imageIndex];
    v.transition(cmd, image, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = v.swapchainViews[v.imageIndex];
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.clearValue.color = {{0.08f, 0.08f, 0.10f, 1.0f}};

    VkRenderingInfo renderingInfo{VK_STRUCTURE_TYPE_RENDERING_INFO};
    renderingInfo.renderArea = {{0, 0}, v.swapchain.extent};
    renderingInfo.layerCount = 1;
    renderingInfo.colorAttachmentCount = 1;
    renderingInfo.pColorAttachments = &color;
    vkCmdBeginRendering(cmd, &renderingInfo);

    VkViewport viewport{0.0f, 0.0f, static_cast<float>(v.swapchain.extent.width), static_cast<float>(v.swapchain.extent.height), 0.0f, 1.0f};
    VkRect2D scissor{{0, 0}, v.swapchain.extent};
    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor(cmd, 0, 1, &scissor);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, v.pipeline);
    vkCmdDraw(cmd, 3, 1, 0, 0);
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), cmd);

    vkCmdEndRendering(cmd);
    v.transition(cmd, image, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
        VK_PIPELINE_STAGE_2_NONE, 0);
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
    if (!check(vkQueueSubmit(v.graphicsQueue, 1, &submit, v.inFlight[v.frame]), "vkQueueSubmit")) {
        // The fence would never signal; put it back so the next wait returns.
        VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
        fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        vkDestroyFence(v.device.device, v.inFlight[v.frame], nullptr);
        check(vkCreateFence(v.device.device, &fenceInfo, nullptr, &v.inFlight[v.frame]), "vkCreateFence");
        v.frame = (v.frame + 1) % kFramesInFlight;
        return;
    }

    VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &v.renderFinished[v.imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &v.swapchain.swapchain;
    present.pImageIndices = &v.imageIndex;
    const VkResult presented = vkQueuePresentKHR(v.graphicsQueue, &present);
    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        if (!v.recreateSwapchain()) {
            log(LogLevel::Warn, "vulkan: swapchain recreation failed after present");
        }
    } else {
        check(presented, "vkQueuePresentKHR");
    }

    v.frame = (v.frame + 1) % kFramesInFlight;
    FrameMark;
}

}
