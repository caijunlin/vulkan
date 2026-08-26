#include "VulkanEngine.h"
#include <android/log.h>
#include <stdexcept>
#include <cmath>
#include <android/asset_manager.h>

const std::vector<const char *> deviceExtensions = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME,
        "VK_ANDROID_external_memory_android_hardware_buffer",
        "VK_KHR_sampler_ycbcr_conversion",
        "VK_KHR_maintenance1",
        "VK_KHR_bind_memory2",
        "VK_KHR_get_memory_requirements2"
};

bool VulkanEngine::init(AAssetManager *mgr) {
    this->assetManager = mgr;
    if (vkInstance != VK_NULL_HANDLE) return true;
    if (!createInstance()) return false;
    if (!pickPhysicalDevice()) return false;
    if (!createLogicalDevice()) return false;
    if (!createCommandPool()) return false;

    fpGetAndroidHardwareBufferPropertiesANDROID =
            (PFN_vkGetAndroidHardwareBufferPropertiesANDROID) vkGetDeviceProcAddr(device,
                                                                                  "vkGetAndroidHardwareBufferPropertiesANDROID");
    if (!fpGetAndroidHardwareBufferPropertiesANDROID) {
        return false;
    }
    return true;
}

std::vector<char> VulkanEngine::readShaderAsset(const char *filename) {
    if (!assetManager) {
        return {};
    }

    AAsset *asset = AAssetManager_open(assetManager, filename, AASSET_MODE_BUFFER);
    if (!asset) {
        return {};
    }

    size_t size = AAsset_getLength(asset);
    std::vector<char> buffer(size);
    AAsset_read(asset, buffer.data(), size);
    AAsset_close(asset);

    return buffer;
}

bool VulkanEngine::buildPipelineForWindow(const std::string &id, WindowContext &ctx,
                                          AHardwareBuffer *firstAhb) {
    VkAndroidHardwareBufferFormatPropertiesANDROID formatProps{
            VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID ahbProps{
            VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID, &formatProps};
    fpGetAndroidHardwareBufferPropertiesANDROID(device, firstAhb, &ahbProps);

    VkExternalFormatANDROID extFormat{VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID};
    extFormat.externalFormat = formatProps.externalFormat;

    VkSamplerYcbcrConversionCreateInfo ycbcrInfo{
            VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_CREATE_INFO};
    ycbcrInfo.pNext = &extFormat;
    ycbcrInfo.format = VK_FORMAT_UNDEFINED;
    ycbcrInfo.ycbcrModel = formatProps.suggestedYcbcrModel;
    ycbcrInfo.ycbcrRange = formatProps.suggestedYcbcrRange;
    ycbcrInfo.components = formatProps.samplerYcbcrConversionComponents;
    ycbcrInfo.xChromaOffset = formatProps.suggestedXChromaOffset;
    ycbcrInfo.yChromaOffset = formatProps.suggestedYChromaOffset;
    ycbcrInfo.chromaFilter = VK_FILTER_LINEAR;
    ycbcrInfo.forceExplicitReconstruction = VK_FALSE;
    vkCreateSamplerYcbcrConversion(device, &ycbcrInfo, nullptr, &ctx.ycbcrConversion);

    VkSamplerYcbcrConversionInfo samplerYcbcrInfo{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    samplerYcbcrInfo.conversion = ctx.ycbcrConversion;

    VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
    samplerInfo.pNext = &samplerYcbcrInfo;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    vkCreateSampler(device, &samplerInfo, nullptr, &ctx.sampler);

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 0;
    binding.descriptorCount = 1;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    binding.pImmutableSamplers = &ctx.sampler; // 【这是绝对不能改的契约】

    VkDescriptorSetLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
    layoutInfo.bindingCount = 1;
    layoutInfo.pBindings = &binding;
    vkCreateDescriptorSetLayout(device, &layoutInfo, nullptr, &ctx.descriptorSetLayout);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1};
    VkDescriptorPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    poolInfo.maxSets = 1;
    vkCreateDescriptorPool(device, &poolInfo, nullptr, &ctx.descriptorPool);

    VkDescriptorSetAllocateInfo allocSetInfo{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
    allocSetInfo.descriptorPool = ctx.descriptorPool;
    allocSetInfo.descriptorSetCount = 1;
    allocSetInfo.pSetLayouts = &ctx.descriptorSetLayout;
    vkAllocateDescriptorSets(device, &allocSetInfo, &ctx.descriptorSet);

    // =========================================================================
    // === 新增：定义并配置 Push Constant 给 Pipeline Layout (用于传递旋转矩阵) ===
    // =========================================================================
    VkPushConstantRange pushConstantRange{};
    pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
    pushConstantRange.offset = 0;
    pushConstantRange.size = sizeof(PushConstantData);

    VkPipelineLayoutCreateInfo pipelineLayoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
    pipelineLayoutInfo.setLayoutCount = 1;
    pipelineLayoutInfo.pSetLayouts = &ctx.descriptorSetLayout;
    pipelineLayoutInfo.pushConstantRangeCount = 1;                    // === 新增 ===
    pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;      // === 新增 ===
    vkCreatePipelineLayout(device, &pipelineLayoutInfo, nullptr, &ctx.pipelineLayout);

    auto vertSpv = readShaderAsset("shaders/triangle_vert.spv");
    auto fragSpv = readShaderAsset("shaders/triangle_frag.spv");

    VkShaderModule vertShaderModule = createShaderModule(vertSpv);
    VkShaderModule fragShaderModule = createShaderModule(fragSpv);

    VkPipelineShaderStageCreateInfo shaderStages[] = {
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_VERTEX_BIT,   vertShaderModule, "main", nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, nullptr, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fragShaderModule, "main", nullptr}
    };

    VkPipelineVertexInputStateCreateInfo vertexInputInfo{
            VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
    VkPipelineInputAssemblyStateCreateInfo inputAssembly{
            VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
    inputAssembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

    VkViewport viewport{0.0f, 0.0f, (float) ctx.extent.width, (float) ctx.extent.height, 0.0f,
                        1.0f};
    VkRect2D scissor{{0, 0}, ctx.extent};
    VkPipelineViewportStateCreateInfo viewportState{
            VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO, nullptr, 0, 1, &viewport, 1,
            &scissor};

    VkPipelineRasterizationStateCreateInfo rasterizer{
            VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
    rasterizer.polygonMode = VK_POLYGON_MODE_FILL;
    rasterizer.lineWidth = 1.0f;
    rasterizer.cullMode = VK_CULL_MODE_BACK_BIT;
    rasterizer.frontFace = VK_FRONT_FACE_CLOCKWISE;

    VkPipelineMultisampleStateCreateInfo multisampling{
            VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
    multisampling.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;

    VkPipelineColorBlendAttachmentState colorBlendAttachment{};
    colorBlendAttachment.colorWriteMask = 0xF;
    colorBlendAttachment.blendEnable = VK_FALSE;
    colorBlendAttachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
    colorBlendAttachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
    colorBlendAttachment.colorBlendOp = VK_BLEND_OP_ADD;

    VkPipelineColorBlendStateCreateInfo colorBlending{
            VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
    colorBlending.attachmentCount = 1;
    colorBlending.pAttachments = &colorBlendAttachment;

    VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
    pipelineInfo.stageCount = 2;
    pipelineInfo.pStages = shaderStages;
    pipelineInfo.pVertexInputState = &vertexInputInfo;
    pipelineInfo.pInputAssemblyState = &inputAssembly;
    pipelineInfo.pViewportState = &viewportState;
    pipelineInfo.pRasterizationState = &rasterizer;
    pipelineInfo.pMultisampleState = &multisampling;
    pipelineInfo.pColorBlendState = &colorBlending;
    pipelineInfo.layout = ctx.pipelineLayout;
    pipelineInfo.renderPass = ctx.renderPass;
    vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr,
                              &ctx.graphicsPipeline);

    vkDestroyShaderModule(device, fragShaderModule, nullptr);
    vkDestroyShaderModule(device, vertShaderModule, nullptr);

    ctx.commandBuffers.resize(ctx.framebuffers.size());
    VkCommandBufferAllocateInfo allocCmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    allocCmdInfo.commandPool = commandPool;
    allocCmdInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocCmdInfo.commandBufferCount = (uint32_t) ctx.commandBuffers.size();
    vkAllocateCommandBuffers(device, &allocCmdInfo, ctx.commandBuffers.data());

    return true;
}

void VulkanEngine::updateVideoTexture(const std::string &id, AHardwareBuffer *ahb) {
    auto it = windows.find(id);
    if (it == windows.end() || !ahb) return;
    auto &ctx = it->second;

    vkWaitForFences(device, ctx.inFlightFences.size(), ctx.inFlightFences.data(), VK_TRUE,
                    UINT64_MAX);

    if (ctx.graphicsPipeline == VK_NULL_HANDLE) {
        if (!buildPipelineForWindow(id, ctx, ahb)) return;
    }

    // 清理上一帧纯粹的图像数据，但不碰任何采样器
    if (ctx.currentVideoTexture.image != VK_NULL_HANDLE) {
        vkDestroyImageView(device, ctx.currentVideoTexture.view, nullptr);
        vkDestroyImage(device, ctx.currentVideoTexture.image, nullptr);
        vkFreeMemory(device, ctx.currentVideoTexture.memory, nullptr);
        ctx.currentVideoTexture = {};
    }

    VkAndroidHardwareBufferFormatPropertiesANDROID formatProps{
            VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_FORMAT_PROPERTIES_ANDROID};
    VkAndroidHardwareBufferPropertiesANDROID ahbProps{
            VK_STRUCTURE_TYPE_ANDROID_HARDWARE_BUFFER_PROPERTIES_ANDROID, &formatProps};
    fpGetAndroidHardwareBufferPropertiesANDROID(device, ahb, &ahbProps);

    VkExternalMemoryImageCreateInfo extMemInfo{VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO};
    extMemInfo.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_ANDROID_HARDWARE_BUFFER_BIT_ANDROID;

    VkExternalFormatANDROID extFormat{VK_STRUCTURE_TYPE_EXTERNAL_FORMAT_ANDROID};
    extFormat.externalFormat = formatProps.externalFormat;
    extFormat.pNext = &extMemInfo;

    AHardwareBuffer_Desc desc;
    AHardwareBuffer_describe(ahb, &desc);

    VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageInfo.pNext = &extFormat;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.format = VK_FORMAT_UNDEFINED;
    imageInfo.extent = {desc.width, desc.height, 1};
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;

    if (vkCreateImage(device, &imageInfo, nullptr, &ctx.currentVideoTexture.image) !=
        VK_SUCCESS)
        return;

    VkImportAndroidHardwareBufferInfoANDROID importInfo{
            VK_STRUCTURE_TYPE_IMPORT_ANDROID_HARDWARE_BUFFER_INFO_ANDROID};
    importInfo.buffer = ahb;

    VkMemoryDedicatedAllocateInfo dedicatedInfo{VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO};
    dedicatedInfo.pNext = &importInfo;
    dedicatedInfo.image = ctx.currentVideoTexture.image;

    VkMemoryAllocateInfo allocInfo{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    allocInfo.pNext = &dedicatedInfo;
    allocInfo.allocationSize = ahbProps.allocationSize;
    allocInfo.memoryTypeIndex = findMemoryType(ahbProps.memoryTypeBits, 0);

    vkAllocateMemory(device, &allocInfo, nullptr, &ctx.currentVideoTexture.memory);
    vkBindImageMemory(device, ctx.currentVideoTexture.image, ctx.currentVideoTexture.memory, 0);
    VkSamplerYcbcrConversionInfo ycbcrViewInfo{VK_STRUCTURE_TYPE_SAMPLER_YCBCR_CONVERSION_INFO};
    ycbcrViewInfo.conversion = ctx.ycbcrConversion;

    VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.pNext = &ycbcrViewInfo;
    viewInfo.image = ctx.currentVideoTexture.image;
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = VK_FORMAT_UNDEFINED;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.layerCount = 1;
    vkCreateImageView(device, &viewInfo, nullptr, &ctx.currentVideoTexture.view);

    // 复用 Immutable Sampler 更新描述符！
    VkDescriptorImageInfo descImageInfo{};
    descImageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    descImageInfo.imageView = ctx.currentVideoTexture.view;
    descImageInfo.sampler = ctx.sampler;

    VkWriteDescriptorSet descriptorWrite{VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};
    descriptorWrite.dstSet = ctx.descriptorSet;
    descriptorWrite.dstBinding = 0;
    descriptorWrite.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    descriptorWrite.descriptorCount = 1;
    descriptorWrite.pImageInfo = &descImageInfo;

    vkUpdateDescriptorSets(device, 1, &descriptorWrite, 0, nullptr);
}

void VulkanEngine::recordCommandBuffer(WindowContext &ctx, uint32_t imageIndex) {
    VkCommandBufferBeginInfo beginInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    vkBeginCommandBuffer(ctx.commandBuffers[imageIndex], &beginInfo);

    if (ctx.currentVideoTexture.image != VK_NULL_HANDLE) {
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
        barrier.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        barrier.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
        barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        barrier.image = ctx.currentVideoTexture.image;
        barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        barrier.subresourceRange.levelCount = 1;
        barrier.subresourceRange.layerCount = 1;
        barrier.srcAccessMask = 0;
        barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

        vkCmdPipelineBarrier(ctx.commandBuffers[imageIndex], VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
                             VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, nullptr, 0, nullptr, 1,
                             &barrier);
    }

    VkRenderPassBeginInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    renderPassInfo.renderPass = ctx.renderPass;
    renderPassInfo.framebuffer = ctx.framebuffers[imageIndex];
    renderPassInfo.renderArea.extent = ctx.extent;
    VkClearValue clearColor = {{{0.0f, 0.0f, 0.0f, 1.0f}}};
    renderPassInfo.clearValueCount = 1;
    renderPassInfo.pClearValues = &clearColor;

    vkCmdBeginRenderPass(ctx.commandBuffers[imageIndex], &renderPassInfo,
                         VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(ctx.commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                      ctx.graphicsPipeline);

    VkViewport viewport{0.0f, 0.0f, (float) ctx.extent.width, (float) ctx.extent.height, 0.0f,
                        1.0f};
    vkCmdSetViewport(ctx.commandBuffers[imageIndex], 0, 1, &viewport);
    VkRect2D scissor{{0, 0}, ctx.extent};
    vkCmdSetScissor(ctx.commandBuffers[imageIndex], 0, 1, &scissor);

    if (ctx.currentVideoTexture.image != VK_NULL_HANDLE) {
        vkCmdBindDescriptorSets(ctx.commandBuffers[imageIndex], VK_PIPELINE_BIND_POINT_GRAPHICS,
                                ctx.pipelineLayout, 0, 1, &ctx.descriptorSet, 0, nullptr);
    }

    vkCmdPushConstants(
            ctx.commandBuffers[imageIndex],
            ctx.pipelineLayout,
            VK_SHADER_STAGE_VERTEX_BIT,
            0,
            sizeof(PushConstantData),
            &ctx.pushConstantData
    );

    vkCmdDraw(ctx.commandBuffers[imageIndex], 6, 1, 0, 0);
    vkCmdEndRenderPass(ctx.commandBuffers[imageIndex]);
    vkEndCommandBuffer(ctx.commandBuffers[imageIndex]);
}

void VulkanEngine::drawFrame(const std::string &id) {
    auto it = windows.find(id);
    if (it == windows.end()) return;
    auto &ctx = it->second;

    if (ctx.commandBuffers.empty()) return;

    vkWaitForFences(device, 1, &ctx.inFlightFences[ctx.currentFrame], VK_TRUE, UINT64_MAX);

    uint32_t imageIndex;
    VkResult result = vkAcquireNextImageKHR(device, ctx.swapchain, UINT64_MAX,
                                            ctx.imageAvailableSemaphores[ctx.currentFrame],
                                            VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR) {
        return;
    } else if (result == VK_SUBOPTIMAL_KHR) {
    } else if (result != VK_SUCCESS) {
        return;
    }

    vkResetFences(device, 1, &ctx.inFlightFences[ctx.currentFrame]);
    vkResetCommandBuffer(ctx.commandBuffers[imageIndex], 0);
    recordCommandBuffer(ctx, imageIndex);

    VkSubmitInfo submitInfo{VK_STRUCTURE_TYPE_SUBMIT_INFO};
    VkSemaphore waitSemaphores[] = {ctx.imageAvailableSemaphores[ctx.currentFrame]};
    VkPipelineStageFlags waitStages[] = {VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
    submitInfo.waitSemaphoreCount = 1;
    submitInfo.pWaitSemaphores = waitSemaphores;
    submitInfo.pWaitDstStageMask = waitStages;
    submitInfo.commandBufferCount = 1;
    submitInfo.pCommandBuffers = &ctx.commandBuffers[imageIndex];

    VkSemaphore signalSemaphores[] = {ctx.renderFinishedSemaphores[ctx.currentFrame]};
    submitInfo.signalSemaphoreCount = 1;
    submitInfo.pSignalSemaphores = signalSemaphores;

    vkQueueSubmit(graphicsQueue, 1, &submitInfo, ctx.inFlightFences[ctx.currentFrame]);

    VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    presentInfo.waitSemaphoreCount = 1;
    presentInfo.pWaitSemaphores = signalSemaphores;
    VkSwapchainKHR swapchains[] = {ctx.swapchain};
    presentInfo.swapchainCount = 1;
    presentInfo.pSwapchains = swapchains;
    presentInfo.pImageIndices = &imageIndex;
    vkQueuePresentKHR(graphicsQueue, &presentInfo);

    ctx.currentFrame = (ctx.currentFrame + 1) % ctx.framebuffers.size();
}

void VulkanEngine::removeWindow(const std::string &id) {
    auto it = windows.find(id);
    if (it != windows.end()) {
        vkDeviceWaitIdle(device);
        auto &ctx = it->second;

        vkFreeCommandBuffers(device, commandPool, static_cast<uint32_t>(ctx.commandBuffers.size()),
                             ctx.commandBuffers.data());
        for (size_t i = 0; i < ctx.framebuffers.size(); i++) {
            vkDestroySemaphore(device, ctx.imageAvailableSemaphores[i], nullptr);
            vkDestroySemaphore(device, ctx.renderFinishedSemaphores[i], nullptr);
            vkDestroyFence(device, ctx.inFlightFences[i], nullptr);
        }

        if (ctx.currentVideoTexture.image != VK_NULL_HANDLE) {
            vkDestroyImageView(device, ctx.currentVideoTexture.view, nullptr);
            vkDestroyImage(device, ctx.currentVideoTexture.image, nullptr);
            vkFreeMemory(device, ctx.currentVideoTexture.memory, nullptr);
        }

        if (ctx.sampler != VK_NULL_HANDLE) vkDestroySampler(device, ctx.sampler, nullptr);
        if (ctx.ycbcrConversion != VK_NULL_HANDLE)
            vkDestroySamplerYcbcrConversion(device, ctx.ycbcrConversion, nullptr);

        if (ctx.graphicsPipeline != VK_NULL_HANDLE)
            vkDestroyPipeline(device, ctx.graphicsPipeline, nullptr);
        if (ctx.pipelineLayout != VK_NULL_HANDLE)
            vkDestroyPipelineLayout(device, ctx.pipelineLayout, nullptr);
        if (ctx.descriptorPool != VK_NULL_HANDLE)
            vkDestroyDescriptorPool(device, ctx.descriptorPool, nullptr);
        if (ctx.descriptorSetLayout != VK_NULL_HANDLE)
            vkDestroyDescriptorSetLayout(device, ctx.descriptorSetLayout, nullptr);

        for (auto framebuffer: ctx.framebuffers) vkDestroyFramebuffer(device, framebuffer, nullptr);
        if (ctx.renderPass != VK_NULL_HANDLE) vkDestroyRenderPass(device, ctx.renderPass, nullptr);

        for (auto imageView: ctx.imageViews) vkDestroyImageView(device, imageView, nullptr);
        if (ctx.swapchain != VK_NULL_HANDLE) vkDestroySwapchainKHR(device, ctx.swapchain, nullptr);
        if (ctx.surface != VK_NULL_HANDLE) vkDestroySurfaceKHR(vkInstance, ctx.surface, nullptr);

        windows.erase(it);
    }
}

void VulkanEngine::cleanup() {
    if (device == VK_NULL_HANDLE) return;
    vkDeviceWaitIdle(device);
    for (auto &pair: windows) removeWindow(pair.first);
    windows.clear();
    if (commandPool != VK_NULL_HANDLE) vkDestroyCommandPool(device, commandPool, nullptr);
    if (device != VK_NULL_HANDLE) vkDestroyDevice(device, nullptr);
    if (vkInstance != VK_NULL_HANDLE) vkDestroyInstance(vkInstance, nullptr);
}

bool VulkanEngine::addWindow(const std::string &id, ANativeWindow *window) {
    if (!window) return false;
    if (device != VK_NULL_HANDLE) vkDeviceWaitIdle(device);
    if (windows.find(id) != windows.end()) removeWindow(id);

    WindowContext ctx{};
    VkAndroidSurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR};
    surfaceInfo.window = window;
    if (vkCreateAndroidSurfaceKHR(vkInstance, &surfaceInfo, nullptr, &ctx.surface) != VK_SUCCESS) {
        return false;
    }

    VkSurfaceCapabilitiesKHR capabilities;
    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, ctx.surface, &capabilities);

    uint32_t formatCount;
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, ctx.surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, ctx.surface, &formatCount, formats.data());
    ctx.format = formats[0].format;
    ctx.extent = capabilities.currentExtent;
    if (ctx.extent.width == 0xFFFFFFFF) {
        ctx.extent.width = ANativeWindow_getWidth(window);
        ctx.extent.height = ANativeWindow_getHeight(window);
    }

    VkSwapchainCreateInfoKHR swapchainInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
    swapchainInfo.surface = ctx.surface;
    swapchainInfo.minImageCount = capabilities.minImageCount + 1;
    swapchainInfo.imageFormat = ctx.format;
    swapchainInfo.imageColorSpace = formats[0].colorSpace;
    swapchainInfo.imageExtent = ctx.extent;
    swapchainInfo.imageArrayLayers = 1;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = (capabilities.supportedCompositeAlpha &
                                    VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR)
                                   ? VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR
                                   : VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR;
    swapchainInfo.clipped = VK_TRUE;
    if (vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &ctx.swapchain) != VK_SUCCESS) {
        return false;
    }
    uint32_t imageCount;
    vkGetSwapchainImagesKHR(device, ctx.swapchain, &imageCount, nullptr);
    ctx.images.resize(imageCount);
    vkGetSwapchainImagesKHR(device, ctx.swapchain, &imageCount, ctx.images.data());

    ctx.imageViews.resize(imageCount);
    for (size_t i = 0; i < imageCount; i++) {
        VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
        viewInfo.image = ctx.images[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = ctx.format;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        vkCreateImageView(device, &viewInfo, nullptr, &ctx.imageViews[i]);
    }

    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = ctx.format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorAttachmentRef{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorAttachmentRef;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    renderPassInfo.attachmentCount = 1;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1;
    renderPassInfo.pDependencies = &dependency;
    vkCreateRenderPass(device, &renderPassInfo, nullptr, &ctx.renderPass);

    ctx.framebuffers.resize(ctx.imageViews.size());
    for (size_t i = 0; i < ctx.imageViews.size(); i++) {
        VkImageView attachments[] = {ctx.imageViews[i]};
        VkFramebufferCreateInfo framebufferInfo{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
        framebufferInfo.renderPass = ctx.renderPass;
        framebufferInfo.attachmentCount = 1;
        framebufferInfo.pAttachments = attachments;
        framebufferInfo.width = ctx.extent.width;
        framebufferInfo.height = ctx.extent.height;
        framebufferInfo.layers = 1;
        vkCreateFramebuffer(device, &framebufferInfo, nullptr, &ctx.framebuffers[i]);
    }

    ctx.imageAvailableSemaphores.resize(ctx.framebuffers.size());
    ctx.renderFinishedSemaphores.resize(ctx.framebuffers.size());
    ctx.inFlightFences.resize(ctx.framebuffers.size());
    VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    for (size_t i = 0; i < ctx.framebuffers.size(); i++) {
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &ctx.imageAvailableSemaphores[i]);
        vkCreateSemaphore(device, &semaphoreInfo, nullptr, &ctx.renderFinishedSemaphores[i]);
        vkCreateFence(device, &fenceInfo, nullptr, &ctx.inFlightFences[i]);
    }

    windows[id] = ctx;
    return true;
}

uint32_t VulkanEngine::findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties) {
    VkPhysicalDeviceMemoryProperties memProperties;
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &memProperties);
    for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
        if ((typeFilter & (1 << i)) &&
            (memProperties.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    throw std::runtime_error("Failed to find memory type");
}

VkShaderModule VulkanEngine::createShaderModule(const std::vector<char> &code) {
    VkShaderModuleCreateInfo createInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
    createInfo.codeSize = code.size();
    createInfo.pCode = reinterpret_cast<const uint32_t *>(code.data());
    VkShaderModule shaderModule;
    vkCreateShaderModule(device, &createInfo, nullptr, &shaderModule);
    return shaderModule;
}

bool VulkanEngine::createInstance() {
    VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
    appInfo.apiVersion = VK_API_VERSION_1_1;
    std::vector<const char *> extensions = {"VK_KHR_surface", "VK_KHR_android_surface"};
    VkInstanceCreateInfo createInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    return vkCreateInstance(&createInfo, nullptr, &vkInstance) == VK_SUCCESS;
}

bool VulkanEngine::pickPhysicalDevice() {
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(vkInstance, &deviceCount, nullptr);
    if (deviceCount == 0) return false;
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(vkInstance, &deviceCount, devices.data());
    for (const auto &dev: devices) {
        uint32_t queueFamilyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(dev, &queueFamilyCount, queueFamilies.data());
        for (uint32_t i = 0; i < queueFamilyCount; i++) {
            if (queueFamilies[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) {
                physicalDevice = dev;
                graphicsQueueFamilyIndex = i;
                return true;
            }
        }
    }
    return false;
}

bool VulkanEngine::createLogicalDevice() {
    float queuePriority = 1.0f;
    VkDeviceQueueCreateInfo queueCreateInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
    queueCreateInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    VkPhysicalDeviceFeatures deviceFeatures{};
    VkDeviceCreateInfo createInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    createInfo.pQueueCreateInfos = &queueCreateInfo;
    createInfo.queueCreateInfoCount = 1;
    createInfo.pEnabledFeatures = &deviceFeatures;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size());
    createInfo.ppEnabledExtensionNames = deviceExtensions.data();
    if (vkCreateDevice(physicalDevice, &createInfo, nullptr, &device) != VK_SUCCESS) return false;
    vkGetDeviceQueue(device, graphicsQueueFamilyIndex, 0, &graphicsQueue);
    return true;
}

bool VulkanEngine::createCommandPool() {
    VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamilyIndex;
    return vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) == VK_SUCCESS;
}