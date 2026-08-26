package com.github.caijunlin.vulkan

import android.graphics.Typeface
import android.os.Bundle
import android.text.TextUtils
import android.view.Gravity
import android.view.Surface
import android.view.SurfaceHolder
import android.view.SurfaceView
import android.view.WindowManager
import android.widget.TextView
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.gestures.detectTapGestures
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.width
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.BasicTextField
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Close
import androidx.compose.material.icons.filled.Delete
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.DropdownMenu
import androidx.compose.material3.DropdownMenuItem
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextFieldDefaults
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.DisposableEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateListOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.text.PlatformTextStyle
import androidx.compose.ui.text.TextStyle
import androidx.compose.ui.text.input.VisualTransformation
import androidx.compose.ui.text.style.LineHeightStyle
import androidx.compose.ui.text.style.TextAlign
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.core.view.WindowCompat
import androidx.core.view.WindowInsetsCompat
import androidx.core.view.WindowInsetsControllerCompat
import com.github.caijunlin.vulkan.api.VLCRenderPool
import com.github.caijunlin.vulkan.core.VulkanCore
import com.github.caijunlin.vulkan.ui.theme.VulkanTheme

class MainActivity : ComponentActivity() {

    private data class StreamItem(val id: Int, val url: String)

    private val presetUrls = listOf(
        "rtsp://192.168.1.112/live/sub/av_stream",
        "rtsp://192.168.1.112/live/sub/av_stream?1",
        "rtsp://192.168.1.112/live/sub/av_stream?2",
        "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8",
        "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8?1",
        "https://test-streams.mux.dev/x36xhzz/x36xhzz.m3u8?2"
    )

    private var nextId = 0
    private val items = mutableStateListOf<StreamItem>()
    private val selectedIds = mutableStateListOf<Int>()
    private var selectionMode by mutableStateOf(false)
    private val surfaceViews = mutableMapOf<Int, SurfaceView>()
    private val boundSurfaces = mutableMapOf<Int, Surface>()

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        enableEdgeToEdge()

        window.attributes.layoutInDisplayCutoutMode =
            WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES

        WindowCompat.setDecorFitsSystemWindows(window, false)

        WindowCompat.getInsetsController(window, window.decorView).apply {
            hide(WindowInsetsCompat.Type.systemBars())
            systemBarsBehavior = WindowInsetsControllerCompat.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE
        }

        VLCRenderPool.init(applicationContext)
        VulkanCore.initVulkan(assets)

        setContent {
            VulkanTheme {
                Scaffold(
                    modifier = Modifier.fillMaxSize(),
                    contentWindowInsets = WindowInsets(0, 0, 0, 0)
                ) { innerPadding ->
                    Column(
                        modifier = Modifier
                            .fillMaxSize()
                            .padding(innerPadding)
                    ) {
                        SelectionTopBar()
                        ControlPanel()
                        SurfaceList()
                    }
                }
            }
        }
    }

    /** 顶部操作栏：仅在多选模式下显示小型操作条，隐藏默认 Title */
    @Composable
    private fun SelectionTopBar() {
        if (selectionMode) {
            Row(
                modifier = Modifier
                    .fillMaxWidth()
                    .background(MaterialTheme.colorScheme.surfaceVariant)
                    .padding(horizontal = 8.dp, vertical = 2.dp),
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.SpaceBetween
            ) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    IconButton(
                        onClick = {
                            selectedIds.clear()
                            selectionMode = false
                        }, modifier = Modifier.size(28.dp)
                    ) {
                        Icon(
                            Icons.Default.Close,
                            contentDescription = "取消多选",
                            modifier = Modifier.size(16.dp)
                        )
                    }
                    Spacer(modifier = Modifier.width(4.dp))
                    Text(
                        text = "已选中 ${selectedIds.size} 项", fontSize = 12.sp
                    )
                }
                IconButton(
                    onClick = { deleteSelected() }, modifier = Modifier.size(28.dp)
                ) {
                    Icon(
                        Icons.Default.Delete,
                        contentDescription = "删除选中项",
                        modifier = Modifier.size(16.dp)
                    )
                }
            }
        }
    }

    @Composable
    private fun ControlPanel() {
        var urlText by remember { mutableStateOf(presetUrls.firstOrNull() ?: "") }
        var countText by remember { mutableStateOf("10") }
        var menuExpanded by remember { mutableStateOf(false) }

        val interactionSourceUrl = remember { MutableInteractionSource() }
        val interactionSourceCount = remember { MutableInteractionSource() }

        Row(
            modifier = Modifier
                .fillMaxWidth()
                .padding(horizontal = 6.dp, vertical = 4.dp),
            verticalAlignment = Alignment.CenterVertically
        ) {
            Box(modifier = Modifier.weight(1f)) {
                BasicTextField(
                    value = urlText,
                    onValueChange = { urlText = it },
                    modifier = Modifier
                        .fillMaxWidth()
                        .height(36.dp),
                    textStyle = TextStyle(
                        fontSize = 14.sp, color = MaterialTheme.colorScheme.onSurface
                    ),
                    singleLine = true,
                    interactionSource = interactionSourceUrl
                ) { innerTextField ->
                    OutlinedTextFieldDefaults.DecorationBox(
                        value = urlText,
                        innerTextField = innerTextField,
                        enabled = true,
                        singleLine = true,
                        visualTransformation = VisualTransformation.None,
                        interactionSource = interactionSourceUrl,
                        placeholder = {
                            Text(
                                text = "选择或输入 URL",
                                fontSize = 13.sp,
                                color = MaterialTheme.colorScheme.onSurfaceVariant
                            )
                        },
                        contentPadding = PaddingValues(horizontal = 8.dp, vertical = 2.dp),
                        container = {
                            OutlinedTextFieldDefaults.Container(
                                enabled = true,
                                isError = false,
                                interactionSource = interactionSourceUrl,
                                colors = OutlinedTextFieldDefaults.colors()
                            )
                        })
                }

                // 右侧下拉按钮
                Button(
                    onClick = { menuExpanded = true },
                    modifier = Modifier
                        .align(Alignment.CenterEnd)
                        .height(28.dp)
                        .padding(end = 4.dp),
                    contentPadding = PaddingValues(horizontal = 8.dp, vertical = 0.dp)
                ) {
                    Text("▾", fontSize = 12.sp)
                }

                DropdownMenu(
                    expanded = menuExpanded, onDismissRequest = { menuExpanded = false }) {
                    presetUrls.forEach { url ->
                        DropdownMenuItem(text = {
                            Text(
                                url,
                                fontSize = 12.sp,
                                maxLines = 1,
                                overflow = TextOverflow.Ellipsis
                            )
                        }, onClick = {
                            urlText = url
                            menuExpanded = false
                        })
                    }
                }
            }

            Spacer(modifier = Modifier.width(6.dp))

            BasicTextField(
                value = countText,
                onValueChange = { countText = it },
                modifier = Modifier
                    .width(52.dp)
                    .height(36.dp),
                textStyle = TextStyle(
                    fontSize = 14.sp, color = MaterialTheme.colorScheme.onSurface
                ),
                singleLine = true,
                interactionSource = interactionSourceCount
            ) { innerTextField ->
                OutlinedTextFieldDefaults.DecorationBox(
                    value = countText,
                    innerTextField = innerTextField,
                    enabled = true,
                    singleLine = true,
                    visualTransformation = VisualTransformation.None,
                    interactionSource = interactionSourceCount,
                    contentPadding = PaddingValues(horizontal = 6.dp, vertical = 2.dp),
                    container = {
                        OutlinedTextFieldDefaults.Container(
                            enabled = true,
                            isError = false,
                            interactionSource = interactionSourceCount,
                            colors = OutlinedTextFieldDefaults.colors()
                        )
                    })
            }

            Spacer(modifier = Modifier.width(6.dp))

            // --- 创建按钮 ---
            Button(
                onClick = {
                    val url = urlText.trim()
                    if (url.isNotEmpty()) {
                        val count = countText.toIntOrNull()?.coerceIn(1, 50) ?: 1
                        repeat(count) {
                            items.add(StreamItem(nextId++, url))
                            VLCRenderPool.startDecodeTask(url)
                        }
                    }
                },
                modifier = Modifier.height(36.dp),
                contentPadding = PaddingValues(horizontal = 12.dp, vertical = 0.dp)
            ) {
                Text("创建", fontSize = 12.sp)
            }
        }
    }

    @Composable
    private fun SurfaceList() {
        LazyVerticalGrid(
            columns = GridCells.Adaptive(minSize = 150.dp),
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(6.dp),
            verticalArrangement = Arrangement.spacedBy(6.dp),
            horizontalArrangement = Arrangement.spacedBy(6.dp)
        ) {
            items(items, key = { it.id }) { item ->
                StreamCard(item)
            }
        }
    }

    /** 小型 Surface 卡片 */
    @Composable
    private fun StreamCard(item: StreamItem) {
        val isSelected = item.id in selectedIds

        DisposableEffect(item.id) {
            onDispose {
                boundSurfaces.remove(item.id)?.let { VulkanCore.detachSurface(it) }
                surfaceViews.remove(item.id)
            }
        }


        Card(
            modifier = Modifier.fillMaxWidth(),
            shape = RoundedCornerShape(0.dp),
            border = if (isSelected) BorderStroke(
                2.dp, MaterialTheme.colorScheme.primary
            ) else null,
            colors = CardDefaults.cardColors(
                containerColor = if (isSelected) {
                    MaterialTheme.colorScheme.primaryContainer.copy(alpha = 0.3f)
                } else {
                    MaterialTheme.colorScheme.surfaceVariant
                }
            )
        ) {
            Box {
                Column {
                    Box(
                        modifier = Modifier
                            .fillMaxWidth()
                            .height(100.dp)
                            .background(Color.Black)
                    ) {
                        AndroidView(
                            factory = { context ->
                                SurfaceView(context).also { view ->
                                    surfaceViews[item.id] = view
                                    view.holder.addCallback(object : SurfaceHolder.Callback {
                                        override fun surfaceCreated(holder: SurfaceHolder) {
                                            boundSurfaces[item.id] = holder.surface
                                            VulkanCore.attachSurface(item.url, holder.surface)
                                        }

                                        override fun surfaceChanged(
                                            holder: SurfaceHolder,
                                            format: Int,
                                            width: Int,
                                            height: Int
                                        ) {
                                        }

                                        override fun surfaceDestroyed(holder: SurfaceHolder) {
                                            if (boundSurfaces[item.id] === holder.surface) {
                                                boundSurfaces.remove(item.id)
                                                VulkanCore.detachSurface(holder.surface)
                                            }
                                        }
                                    })
                                }
                            }, modifier = Modifier.fillMaxSize()
                        )

                        AndroidView(
                            modifier = Modifier
                                .align(Alignment.BottomStart)
                                .fillMaxWidth()
                                .background(Color.Black.copy(alpha = 0.5f)),
                            factory = { context ->
                                TextView(context).apply {
                                    typeface = Typeface.SANS_SERIF
                                    // 居中且靠上对齐
                                    gravity = Gravity.TOP or Gravity.CENTER_HORIZONTAL
                                    isSingleLine = true
                                    // 中间省略
                                    ellipsize = TextUtils.TruncateAt.MIDDLE
                                    isClickable = false
                                    setTextColor(android.graphics.Color.WHITE) // 白色字体
                                    textSize = 9f // 9sp
                                    includeFontPadding = false // 去除默认的上下留白
                                    // 左右 4dp 的 padding，上下 0dp
                                    val paddingPx =
                                        (4 * context.resources.displayMetrics.density).toInt()
                                    setPadding(paddingPx, 0, paddingPx, 0)
                                }
                            },
                            update = { textView ->
                                textView.text = item.url
                            }
                        )
                    }
                }

                // 透明覆盖层控制选择逻辑
                Box(
                    modifier = Modifier
                        .matchParentSize()
                        .pointerInput(item.id) {
                            detectTapGestures(onTap = {
                                if (selectionMode) {
                                    if (isSelected) {
                                        selectedIds.remove(item.id)
                                        if (selectedIds.isEmpty()) {
                                            selectionMode = false
                                        }
                                    } else {
                                        selectedIds.add(item.id)
                                    }
                                }
                            }, onLongPress = {
                                if (!selectionMode) {
                                    selectionMode = true
                                }
                                if (!isSelected) {
                                    selectedIds.add(item.id)
                                }
                            })
                        })
            }
        }
    }

    private fun deleteSelected() {
        val toDelete = selectedIds.toList()
        for (id in toDelete) {
            val item = items.firstOrNull { it.id == id } ?: continue
            boundSurfaces.remove(id)?.let { VulkanCore.detachSurface(it) }
            surfaceViews.remove(id)
            VLCRenderPool.stopDecodeTask(item.url)

            items.removeAll { it.id == id }
            selectedIds.remove(id)
        }
        selectionMode = false
    }

    override fun onDestroy() {
        super.onDestroy()
        boundSurfaces.values.forEach { VulkanCore.detachSurface(it) }
        items.forEach { VLCRenderPool.stopDecodeTask(it.url) }
        VulkanCore.releaseAll()
    }
}