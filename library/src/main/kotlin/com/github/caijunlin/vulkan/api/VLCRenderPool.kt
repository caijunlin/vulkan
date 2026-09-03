package com.github.caijunlin.vulkan.api

import android.content.Context
import android.util.Log
import androidx.annotation.Keep
import androidx.core.net.toUri
import com.github.caijunlin.vulkan.api.VLCRenderPool.activePlayers
import com.github.caijunlin.vulkan.api.VLCRenderPool.init
import com.github.caijunlin.vulkan.api.VLCRenderPool.startDecodeTask
import com.github.caijunlin.vulkan.api.VLCRenderPool.viewersCount
import com.github.caijunlin.vulkan.core.VulkanCore
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.videolan.libvlc.LibVLC
import org.videolan.libvlc.Media
import org.videolan.libvlc.MediaPlayer
import org.videolan.libvlc.interfaces.IMedia
import kotlin.time.Duration.Companion.milliseconds

/**
 * 基于 LibVLC 的视频解码池，负责按需启动 / 停止视频解码。
 *
 * 设计要点：
 * 一个 URL 对应一个离屏 Surface 和一个 MediaPlayer，通过 [activePlayers] 管理；
 * 使用 [viewersCount] 做引用计数，多个观看者共用同一路解码，最后一个离开时才真正释放；
 * 启动 / 停止均带防抖（debounce），避免短时间内反复创建、销毁解码资源。
 */
object VLCRenderPool {

    private var appContext: Context? = null

    /** LibVLC 实例的可空后备字段，由 [init] 延迟初始化，避免多次创建。 */
    private var _libVlc: LibVLC? = null

    /**
     * LibVLC 实例的只读访问器。
     *
     * 若在 [init] 之前访问会抛出 [IllegalStateException]，用于强制调用方先完成初始化。
     */
    private val libVlc: LibVLC
        get() = _libVlc
            ?: throw IllegalStateException("Vulkan uninitialized.")

    /**
     * 防抖任务所属的协程作用域。
     *
     * 运行在主线程（Dispatchers.Main），并使用 SupervisorJob 保证单个子任务失败
     * 不会影响其它 URL 的解码任务。
     */
    private val scope = CoroutineScope(Dispatchers.Main + SupervisorJob())

    /** 记录每个 URL 当前挂起的防抖任务，Key 为 URL，用于防抖时的取消与重建。 */
    private val debounceJobs = mutableMapOf<String, Job>()

    /** 当前正在解码的 MediaPlayer 集合，Key 为 URL。 */
    private val activePlayers = mutableMapOf<String, MediaPlayer>()

    /** 每个 URL 的观看者（引用）计数，用于判断某路解码何时可以被释放。 */
    private val viewersCount = mutableMapOf<String, Int>()

    /**
     * 初始化全局 LibVLC 实例。
     *
     * 使用 applicationContext 避免泄漏 Activity 上下文；无音频输出；
     * 网络缓存 300ms 并丢弃迟到帧，以保证低延迟。
     *
     * @param context 任意 Context，内部会转换为 applicationContext。
     */
    @Keep
    @JvmStatic
    fun init(context: Context) {
        this.appContext = context
        if (_libVlc == null) {
            Log.d("Vulkan", "LibVLC start up.")
            // 使用 applicationContext 防止内存泄漏
            _libVlc = LibVLC(
                context.applicationContext,
                arrayListOf("--no-audio", "--network-caching=300", "--drop-late-frames")
            )
        }
    }

    /**
     * 启动（或复用）指定 URL 的视频解码任务。
     *
     * 流程：
     * 引用计数 +1；
     * 若该 URL 已在解码则直接返回；
     * 取消可能挂起的停止任务，经过 500ms 防抖后：
     *    - 在 IO 线程解析网络媒体；
     *    - 创建离屏 Surface，并让 MediaPlayer 将视频渲染到该 Surface；
     *    - 开始播放并加入 [activePlayers]。
     *
     * @param url 视频地址，同时作为解码任务的唯一标识。
     */
    @Keep
    @JvmStatic
    fun startDecodeTask(url: String) {
        val currentCount = viewersCount.getOrDefault(url, 0)
        viewersCount[url] = currentCount + 1
        if (activePlayers.containsKey(url)) return

        debounceJobs[url]?.cancel()
        debounceJobs[url] = scope.launch {
            delay(500.milliseconds)
            val media = Media(libVlc, url.toUri())
            withContext(Dispatchers.IO) {
                media.parse(IMedia.Parse.FetchNetwork)
            }

            val videoWidth = 320
            val videoHeight = 193
            val headlessSurface = VulkanCore.createHeadlessSurface(url, videoWidth, videoHeight)
            val mediaPlayer = MediaPlayer(libVlc)
            mediaPlayer.aspectRatio = "$videoWidth:$videoHeight"
            mediaPlayer.media = media
            mediaPlayer.vlcVout.setWindowSize(videoWidth, videoHeight)
            mediaPlayer.vlcVout.setVideoSurface(headlessSurface, null)
            mediaPlayer.vlcVout.attachViews()
            mediaPlayer.play()
            activePlayers[url] = mediaPlayer
            Log.d("Vulkan", "Decode Started for: $url")
        }
    }

    /**
     * 停止（或减少引用）指定 URL 的视频解码任务。
     *
     * 流程：
     * 引用计数 -1（最低为 0）；
     * 若引用计数归零：
     *    - 取消挂起的启动任务；
     *    - 经过 1000ms 防抖后停止并释放 MediaPlayer，销毁对应离屏 Surface。
     *
     * @param url 视频地址，与 [startDecodeTask] 中的 url 一一对应。
     */
    @Keep
    @JvmStatic
    fun stopDecodeTask(url: String) {
        val currentCount = viewersCount.getOrDefault(url, 1) - 1
        viewersCount[url] = currentCount.coerceAtLeast(0)
        if (viewersCount[url] == 0) {
            debounceJobs[url]?.cancel()
            debounceJobs[url] = scope.launch {
                delay(1000.milliseconds)
                activePlayers[url]?.let { player ->
                    player.stop()
                    player.vlcVout.detachViews()
                    player.release()
                }
                activePlayers.remove(url)
                VulkanCore.destroyHeadlessSurface(url)
                Log.d("Vulkan", "Decode Stopped for: $url")
            }
        }
    }
}