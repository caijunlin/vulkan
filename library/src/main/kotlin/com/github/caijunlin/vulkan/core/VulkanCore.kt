package com.github.caijunlin.vulkan.core

import android.content.res.AssetManager
import android.view.Surface
import androidx.annotation.Keep

/**
 * Vulkan 渲染引擎的 JNI 桥接入口。
 */
object VulkanCore {

    /**
     * 在对象初始化时加载本地库 `fork`。
     */
    init {
        System.loadLibrary("fork")
    }

    /**
     * 初始化 Vulkan 渲染环境。
     *
     * @param assetManager 应用的 AssetManager
     */
    @Keep
    @JvmStatic
    external fun initVulkan(assetManager: AssetManager)

    /**
     * 将指定的渲染目标 [Surface] 与某个页面（URL）绑定，并开始由 Vulkan 渲染。
     *
     * @param url 页面标识，用于区分不同的渲染目标，便于回调查找对应上下文。
     * @param surface Android Surface 对象，通常是上层 WebView 提供的绘制表面。
     */
    @Keep
    @JvmStatic
    external fun attachSurface(url: String, surface: Surface)

    /**
     * 解绑并移除之前通过 [attachSurface] 绑定的渲染表面，停止该表面上的 Vulkan 渲染。
     *
     * @param surface 之前绑定的 Surface。
     */
    @Keep
    @JvmStatic
    external fun detachSurface(surface: Surface)

    /**
     * 释放本地层持有的所有资源（窗口、上下文、图形管线等），通常在组件销毁时调用。
     */
    @Keep
    @JvmStatic
    external fun releaseAll()

    /**
     * 创建一个离屏（offscreen / headless）渲染表面，用于在无可见窗口的情况下进行渲染。
     *
     * @param url 页面标识，与普通表面共用同一套 Key 空间。
     * @param width 离屏表面的像素宽度。
     * @param height 离屏表面的像素高度。
     * @return 创建出的 Surface，可再交给其它组件复用。
     */
    external fun createHeadlessSurface(url: String, width: Int, height: Int): Surface

    /**
     * 销毁之前通过 [createHeadlessSurface] 创建的离屏表面。
     *
     * @param url 页面标识，用于定位需要销毁的离屏表面。
     */
    external fun destroyHeadlessSurface(url: String)

}