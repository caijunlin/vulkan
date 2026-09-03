#include <jni.h>
#include <android/native_window_jni.h>
#include <android/hardware_buffer_jni.h>
#include <android/asset_manager_jni.h>
#include "VulkanStreamManager.h"
#include "VulkanEngine.h"

// 显式初始化引擎
extern "C" JNIEXPORT void JNICALL
Java_com_github_caijunlin_vulkan_core_VulkanCore_initVulkan(JNIEnv* env, jclass clazz /* this */, jobject assetManager) {
    AAssetManager* nativeAssetManager = AAssetManager_fromJava(env, assetManager);
    VulkanStreamManager::getInstance().init(nativeAssetManager);
}

// 挂载物理显示屏幕
extern "C" JNIEXPORT void JNICALL
Java_com_github_caijunlin_vulkan_core_VulkanCore_attachSurface(JNIEnv* env, jclass clazz /* this */, jstring urlObj, jobject surface) {
    const char *url = env->GetStringUTFChars(urlObj, nullptr);
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window) {
        char surface_id[64];
        snprintf(surface_id, sizeof(surface_id), "surf_%p", window);
        VulkanStreamManager::getInstance().attachSurface(url, surface_id, window);
        ANativeWindow_release(window);
    }
    env->ReleaseStringUTFChars(urlObj, url);
}

// 卸载物理显示屏幕
extern "C" JNIEXPORT void JNICALL
Java_com_github_caijunlin_vulkan_core_VulkanCore_detachSurface(JNIEnv* env, jclass clazz /* this */, jobject surface) {
    ANativeWindow* window = ANativeWindow_fromSurface(env, surface);
    if (window) {
        char surface_id[64];
        snprintf(surface_id, sizeof(surface_id), "surf_%p", window);
        VulkanStreamManager::getInstance().detachSurface(surface_id);
        ANativeWindow_release(window);
    }
}

// 在 C++ 申请幽灵内存，然后封装为 Java Surface 送给 VLC
extern "C" JNIEXPORT jobject JNICALL
Java_com_github_caijunlin_vulkan_core_VulkanCore_createHeadlessSurface(JNIEnv* env, jobject /* this */, jstring urlObj, jint width, jint height) {
    const char *url = env->GetStringUTFChars(urlObj, nullptr);

    // 透传 width 和 height 给底层
    ANativeWindow* window = VulkanStreamManager::getInstance().createHeadlessReader(url, width, height);

    jobject java_surface = ANativeWindow_toSurface(env, window);
    env->ReleaseStringUTFChars(urlObj, url);
    return java_surface;
}

// 销毁幽灵内存
extern "C" JNIEXPORT void JNICALL
Java_com_github_caijunlin_vulkan_core_VulkanCore_destroyHeadlessSurface(JNIEnv* env, jobject /* this */, jstring urlObj) {
    const char *url = env->GetStringUTFChars(urlObj, nullptr);
    VulkanStreamManager::getInstance().destroyHeadlessReader(url);
    env->ReleaseStringUTFChars(urlObj, url);
}

// 销毁一切资源
extern "C" JNIEXPORT void JNICALL
Java_com_github_caijunlin_vulkan_core_VulkanCore_releaseAll(JNIEnv* env, jclass clazz /* this */) {
    VulkanStreamManager::getInstance().releaseAll();
}
