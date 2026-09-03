#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <media/NdkImageReader.h>
#include <android/native_window.h>
#include <android/asset_manager.h>

struct StreamContext {
    std::string url;
    AImageReader *image_reader = nullptr;
    ANativeWindow *native_window = nullptr;
    std::unordered_set<std::string> bound_surfaces;
    AImage *current_image = nullptr;
};

class CVulkanStreamManager {
public:
    static CVulkanStreamManager &getInstance() {
        static CVulkanStreamManager instance;
        return instance;
    }

    void init(AAssetManager *assetManager);

    ANativeWindow *createHeadlessReader(const std::string &url, int width, int height);

    void destroyHeadlessReader(const std::string &url);

    void
    attachSurface(const std::string &url, const std::string &surface_id, ANativeWindow *window);

    void detachSurface(const std::string &surface_id);

    void releaseAll();

    void pushFrameToSurfaces(const std::string &url, AHardwareBuffer *ahb, AImage *new_image);

private:
    CVulkanStreamManager() = default;

    ~CVulkanStreamManager() = default;

    std::unordered_map<std::string, StreamContext> streams;
    std::unordered_map<std::string, std::string> surface_to_url;
    std::mutex mtx;
};