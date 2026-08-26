# ====================================================================
# 此规则会自动应用到集成 Prism SDK 的宿主 App 构建过程中
# ====================================================================

# 保留对外暴露的所有 Callback 回调接口与数据模型
-keep public class com.github.caijunlin.vulkan.callback.** {
    public <methods>;
    public <fields>;
}

# 保留底层 C++ (JNI) 需要硬编码查找的核心数据类和播放控制类
-keep class org.videolan.libvlc.AWindow { *; }
-keep class org.videolan.libvlc.AWindow$* { *; }
-keep class org.videolan.libvlc.util.AndroidUtil { *; }
-keep class org.videolan.libvlc.util.** { *; }
-keep class org.videolan.libvlc.interfaces.** { *; }
-keep class org.videolan.libvlc.LibVLC { *; }
-keep class org.videolan.libvlc.Media { *; }
-keep class org.videolan.libvlc.Media$* { *; }
-keep class org.videolan.libvlc.MediaPlayer { *; }
-keep class org.videolan.libvlc.MediaPlayer$* { *; }
-keep class org.videolan.libvlc.RendererItem { *; }
-keep class org.videolan.libvlc.Dialog { *; }
-keep class org.videolan.libvlc.Dialog$* { *; }
-keep class org.videolan.libvlc.MediaDiscoverer { *; }
-keep class org.videolan.libvlc.RendererDiscoverer { *; }
-keep class org.videolan.libvlc.MediaList { *; }
-keep class org.videolan.libvlc.VLCObject { *; }
-keep class org.videolan.libvlc.interfaces.** { *; }

# 保护所有包含了 native 方法的类和 native 方法本身不被混淆
-keepclasseswithmembernames class org.videolan.libvlc.** {
    native <methods>;
}

-dontwarn dalvik.**
-dontwarn org.videolan.**
-dontwarn com.github.caijunlin.vulkan.**