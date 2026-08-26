#include "RagnarokStreamManager.h"
#include "VulkanEngine.h"
#include <media/NdkImage.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <unistd.h>


void RagnarokStreamManager::init(AAssetManager *assetManager) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!VulkanEngine::getInstance().init(assetManager)) {
    }
}

// 掐断日志刷屏
static void OnImageAvailable(void *context, AImageReader *reader) {
    auto *ctx = static_cast<StreamContext *>(context);
    AImage *new_image = nullptr;

    if (AImageReader_acquireLatestImage(reader, &new_image) == AMEDIA_OK) {
        AHardwareBuffer *ahb = nullptr;
        AImage_getHardwareBuffer(new_image, &ahb);

        if (ahb != nullptr) {
            // 将新帧传给总管处理
            RagnarokStreamManager::getInstance().pushFrameToSurfaces(ctx->url, ahb, new_image);
        } else {
            AImage_delete(new_image); // 只有出错时才立刻释放
        }
    }
}

// 分发函数：安全替换显存
void RagnarokStreamManager::pushFrameToSurfaces(const std::string &url, AHardwareBuffer *ahb,
                                                AImage *new_image) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = streams.find(url);
    if (it == streams.end()) {
        AImage_delete(new_image);
        return;
    }

    // 遍历通知 Vulkan 去采样这块显存
    for (const auto &surface_id: it->second.bound_surfaces) {
        // updateVideoTexture 内部有 vkWaitForFences，它会强制 CPU 等待 GPU 画完上一帧
        VulkanEngine::getInstance().updateVideoTexture(surface_id, ahb);
        VulkanEngine::getInstance().drawFrame(surface_id);
    }

    // 神级生命周期管理：此时 GPU 已经画完上一帧了，可以安全释放老内存了！
    if (it->second.current_image != nullptr) {
        AImage_delete(it->second.current_image);
    }
    // 保存当前新帧，防止被系统收回
    it->second.current_image = new_image;
}

// 画布创建：使用标准 YUV 格式
ANativeWindow *
RagnarokStreamManager::createHeadlessReader(const std::string &url, int width, int height) {
    std::lock_guard<std::mutex> lock(mtx);
    auto &ctx = streams[url];
    ctx.url = url;

    if (!ctx.image_reader) {
        AImageReader_newWithUsage(
                width, height,
                AIMAGE_FORMAT_PRIVATE,
                AHARDWAREBUFFER_USAGE_GPU_SAMPLED_IMAGE,
                3,
                &ctx.image_reader
        );

        AImageReader_ImageListener listener{};
        listener.context = &ctx;
        listener.onImageAvailable = OnImageAvailable;
        AImageReader_setImageListener(ctx.image_reader, &listener);

        AImageReader_getWindow(ctx.image_reader, &ctx.native_window);
    }
    return ctx.native_window;
}

// 别忘了在 releaseAll 和 destroyHeadlessReader 里加上释放 ctx.current_image 的代码
void RagnarokStreamManager::destroyHeadlessReader(const std::string &url) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = streams.find(url);
    if (it != streams.end()) {
        if (it->second.image_reader) {
            AImageReader_delete(it->second.image_reader);
            it->second.image_reader = nullptr;
            it->second.native_window = nullptr;
        }
        if (it->second.current_image) {
            AImage_delete(it->second.current_image);
            it->second.current_image = nullptr;
        }
    }
}

void RagnarokStreamManager::attachSurface(const std::string &url, const std::string &surface_id,
                                          ANativeWindow *window) {
    std::lock_guard<std::mutex> lock(mtx);
    VulkanEngine::getInstance().addWindow(surface_id, window);
    surface_to_url[surface_id] = url;
    streams[url].bound_surfaces.insert(surface_id);
}

void RagnarokStreamManager::detachSurface(const std::string &surface_id) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = surface_to_url.find(surface_id);
    if (it == surface_to_url.end()) return;
    std::string url = it->second;
    surface_to_url.erase(it);

    VulkanEngine::getInstance().removeWindow(surface_id);
    streams[url].bound_surfaces.erase(surface_id);
}

void RagnarokStreamManager::releaseAll() {
    std::lock_guard<std::mutex> lock(mtx);
    for (auto &pair: streams) {
        if (pair.second.image_reader) {
            AImageReader_delete(pair.second.image_reader);
        }
    }
    streams.clear();
    surface_to_url.clear();
    VulkanEngine::getInstance().cleanup();
}
