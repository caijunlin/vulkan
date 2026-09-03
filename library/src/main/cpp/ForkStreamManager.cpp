#include "ForkStreamManager.h"
#include "VulkanEngine.h"
#include <media/NdkImage.h>
#include <android/hardware_buffer.h>
#include <android/log.h>
#include <unistd.h>

void ForkStreamManager::init(AAssetManager *assetManager) {
    std::lock_guard<std::mutex> lock(mtx);
    if (!VulkanEngine::getInstance().init(assetManager)) {
    }
}

static void OnImageAvailable(void *context, AImageReader *reader) {
    auto *ctx = static_cast<StreamContext *>(context);
    AImage *new_image = nullptr;

    if (AImageReader_acquireLatestImage(reader, &new_image) == AMEDIA_OK) {
        AHardwareBuffer *ahb = nullptr;
        AImage_getHardwareBuffer(new_image, &ahb);

        if (ahb != nullptr) {
            ForkStreamManager::getInstance().pushFrameToSurfaces(ctx->url, ahb, new_image);
        } else {
            AImage_delete(new_image);
        }
    }
}

void ForkStreamManager::pushFrameToSurfaces(const std::string &url, AHardwareBuffer *ahb,
                                            AImage *new_image) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = streams.find(url);
    if (it == streams.end()) {
        AImage_delete(new_image);
        return;
    }

    for (const auto &surface_id: it->second.bound_surfaces) {
        VulkanEngine::getInstance().updateVideoTexture(surface_id, ahb);
        VulkanEngine::getInstance().drawFrame(surface_id);
    }

    if (it->second.current_image != nullptr) {
        AImage_delete(it->second.current_image);
    }
    it->second.current_image = new_image;
}

ANativeWindow *
ForkStreamManager::createHeadlessReader(const std::string &url, int width, int height) {
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

void ForkStreamManager::destroyHeadlessReader(const std::string &url) {
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

void ForkStreamManager::attachSurface(const std::string &url, const std::string &surface_id,
                                      ANativeWindow *window) {
    std::lock_guard<std::mutex> lock(mtx);
    VulkanEngine::getInstance().addWindow(surface_id, window);
    surface_to_url[surface_id] = url;
    streams[url].bound_surfaces.insert(surface_id);
}

void ForkStreamManager::detachSurface(const std::string &surface_id) {
    std::lock_guard<std::mutex> lock(mtx);
    auto it = surface_to_url.find(surface_id);
    if (it == surface_to_url.end()) return;
    std::string url = it->second;
    surface_to_url.erase(it);

    VulkanEngine::getInstance().removeWindow(surface_id);
    streams[url].bound_surfaces.erase(surface_id);
}

void ForkStreamManager::releaseAll() {
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
