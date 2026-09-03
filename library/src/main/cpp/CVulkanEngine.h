#pragma once

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_android.h>
#include <android/hardware_buffer.h>
#include <android/asset_manager.h>
#include <vector>
#include <unordered_map>
#include <string>
#include <android/native_window.h>

struct VulkanTexture {
    VkImage image{VK_NULL_HANDLE};
    VkDeviceMemory memory{VK_NULL_HANDLE};
    VkImageView view{VK_NULL_HANDLE};
};

struct WindowContext {
    VkSurfaceKHR surface{VK_NULL_HANDLE};
    VkSwapchainKHR swapchain{VK_NULL_HANDLE};
    VkFormat format;
    VkExtent2D extent;
    std::vector<VkImage> images;
    std::vector<VkImageView> imageViews;
    VkRenderPass renderPass{VK_NULL_HANDLE};
    std::vector<VkFramebuffer> framebuffers;
    VkPipelineLayout pipelineLayout{VK_NULL_HANDLE};
    VkPipeline graphicsPipeline{VK_NULL_HANDLE};
    std::vector<VkCommandBuffer> commandBuffers;
    std::vector<VkSemaphore> imageAvailableSemaphores;
    std::vector<VkSemaphore> renderFinishedSemaphores;
    std::vector<VkFence> inFlightFences;
    uint32_t currentFrame = 0;
    VkDescriptorSetLayout descriptorSetLayout{VK_NULL_HANDLE};
    VkDescriptorPool descriptorPool{VK_NULL_HANDLE};
    VkDescriptorSet descriptorSet{VK_NULL_HANDLE};

    // 硬件转换器和采样器必须与窗口管线同寿，绝对不能每帧销毁！
    VkSamplerYcbcrConversion ycbcrConversion{VK_NULL_HANDLE};
    VkSampler sampler{VK_NULL_HANDLE};

    VulkanTexture currentVideoTexture{};

};

class CVulkanEngine {
public:
    static CVulkanEngine &getInstance() {
        static CVulkanEngine instance;
        return instance;
    }

    bool init(AAssetManager *mgr);

    void cleanup();

    bool addWindow(const std::string &id, ANativeWindow *window);

    void removeWindow(const std::string &id);

    void drawFrame(const std::string &id);

    void updateVideoTexture(const std::string &id, AHardwareBuffer *ahb);

    VkShaderModule createShaderModule(const std::vector<char> &code);

private:
    CVulkanEngine() = default;

    ~CVulkanEngine() = default;

    CVulkanEngine(const CVulkanEngine &) = delete;

    CVulkanEngine &operator=(const CVulkanEngine &) = delete;

    AAssetManager *assetManager{nullptr};
    VkInstance vkInstance{VK_NULL_HANDLE};
    VkPhysicalDevice physicalDevice{VK_NULL_HANDLE};
    VkDevice device{VK_NULL_HANDLE};
    VkQueue graphicsQueue{VK_NULL_HANDLE};
    uint32_t graphicsQueueFamilyIndex{0};
    VkCommandPool commandPool{VK_NULL_HANDLE};

    std::unordered_map<std::string, WindowContext> windows;
    PFN_vkGetAndroidHardwareBufferPropertiesANDROID fpGetAndroidHardwareBufferPropertiesANDROID{
            nullptr};

    bool createInstance();

    bool pickPhysicalDevice();

    bool createLogicalDevice();

    bool createCommandPool();

    bool
    buildPipelineForWindow(const std::string &id, WindowContext &ctx, AHardwareBuffer *firstAhb);

    std::vector<char> readShaderAsset(const char *filename);

    uint32_t findMemoryType(uint32_t typeFilter, VkMemoryPropertyFlags properties);

    static void recordCommandBuffer(WindowContext &ctx, uint32_t imageIndex);
};