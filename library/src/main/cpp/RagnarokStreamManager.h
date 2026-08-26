#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <media/NdkImageReader.h>
#include <android/native_window.h>
#include <android/asset_manager.h>

// 视频流上下文：一个 URL 对应一个幽灵画布和一堆绑定的物理屏幕
struct StreamContext {
    std::string url;
    AImageReader *image_reader = nullptr;
    ANativeWindow *native_window = nullptr;
    std::unordered_set<std::string> bound_surfaces;
    AImage *current_image = nullptr;
};

class RagnarokStreamManager {
public:
    static RagnarokStreamManager &getInstance() {
        static RagnarokStreamManager instance;
        return instance;
    }

    void init(AAssetManager *assetManager);

    // 原生幽灵画布生命周期
    ANativeWindow *createHeadlessReader(const std::string &url, int width, int height);

    void destroyHeadlessReader(const std::string &url);

    // 物理画布 (Surface) 挂载与卸载
    void
    attachSurface(const std::string &url, const std::string &surface_id, ANativeWindow *window);

    void detachSurface(const std::string &surface_id);

    // 清理一切
    void releaseAll();

    static void setOrientation(int degrees);

    // 供底层的 ImageAvailable 回调使用的分发接口
    void pushFrameToSurfaces(const std::string &url, AHardwareBuffer *ahb, AImage *new_image);

private:
    RagnarokStreamManager() = default;

    ~RagnarokStreamManager() = default;

    std::unordered_map<std::string, StreamContext> streams;
    std::unordered_map<std::string, std::string> surface_to_url;
    std::mutex mtx;
};