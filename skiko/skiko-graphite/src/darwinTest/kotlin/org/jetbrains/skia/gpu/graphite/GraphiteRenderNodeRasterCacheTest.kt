package org.jetbrains.skia.gpu.graphite

import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.addressOf
import kotlinx.cinterop.objcPtr
import kotlinx.cinterop.usePinned
import org.jetbrains.skia.*
import org.jetbrains.skia.impl.use
import org.jetbrains.skiko.ExperimentalSkikoApi
import org.jetbrains.skiko.OS
import org.jetbrains.skiko.hostOs
import org.jetbrains.skiko.node.RenderNode
import org.jetbrains.skiko.node.RenderNodeCaches
import org.jetbrains.skiko.node.RenderNodeContext
import platform.Metal.MTLCreateSystemDefaultDevice
import platform.Metal.MTLDeviceProtocol
import platform.Metal.MTLPixelFormatBGRA8Unorm
import platform.Metal.MTLRegionMake2D
import platform.Metal.MTLStorageModeShared
import platform.Metal.MTLTextureDescriptor
import platform.Metal.MTLTextureProtocol
import platform.Metal.MTLTextureUsageRenderTarget
import platform.Metal.MTLTextureUsageShaderRead
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertTrue
import kotlin.test.fail

/**
 * Through Graphite, nodes drawn from the textures they keep (retained layers, the raster cache, promotions) against
 * the same nodes drawn without them, frame after frame; and a recorder going away takes its textures with it.
 */
@OptIn(ExperimentalForeignApi::class, ExperimentalSkikoApi::class)
class GraphiteRenderNodeRasterCacheTest {
    private val width = 320
    private val height = 240

    private class Scene(val context: RenderNodeContext) {
        val nodes = mutableListOf<RenderNode>()

        fun node(bounds: Rect, block: RenderNode.() -> Unit = {}, draw: Canvas.() -> Unit): RenderNode {
            val node = RenderNode(context)
            node.bounds = bounds
            node.block()
            val canvas = node.beginRecording()
            canvas.draw()
            node.endRecording()
            nodes += node
            return node
        }

        fun close() {
            nodes.forEach { it.close() }
            context.close()
        }
    }

    private fun texture(device: MTLDeviceProtocol): MTLTextureProtocol {
        val descriptor = MTLTextureDescriptor.texture2DDescriptorWithPixelFormat(
            MTLPixelFormatBGRA8Unorm, width.toULong(), height.toULong(), false,
        )
        descriptor.usage = MTLTextureUsageShaderRead or MTLTextureUsageRenderTarget
        descriptor.storageMode = MTLStorageModeShared
        return checkNotNull(device.newTextureWithDescriptor(descriptor))
    }

    private fun bytesOf(texture: MTLTextureProtocol): ByteArray {
        val bytes = ByteArray(width * height * 4)
        bytes.usePinned {
            texture.getBytes(it.addressOf(0), (width * 4).toULong(), MTLRegionMake2D(0u, 0u, width.toULong(), height.toULong()), 0u)
        }
        return bytes
    }

    /** Where the last diff found differences. */
    private var lastBox = listOf<Int>()

    private fun diff(a: ByteArray, b: ByteArray): Pair<Int, Int> {
        var differing = 0
        var max = 0
        var box = intArrayOf(Int.MAX_VALUE, Int.MAX_VALUE, -1, -1)
        for (i in a.indices) {
            val d = kotlin.math.abs((a[i].toInt() and 0xFF) - (b[i].toInt() and 0xFF))
            if (d != 0) {
                differing++
                val x = (i / 4) % width; val y = (i / 4) / width
                box = intArrayOf(minOf(box[0], x), minOf(box[1], y), maxOf(box[2], x), maxOf(box[3], y))
            }
            if (d > max) max = d
        }
        lastBox = box.toList()
        return differing to max
    }

    /**
     * Draws [frames] frames of [root] through [recorder], into a cached and an uncached texture each, calling [setup]
     * first; returns each frame's (differing channels, largest difference).
     */
    private fun frames(
        context: GraphiteContext,
        recorder: Recorder,
        device: MTLDeviceProtocol,
        root: RenderNode,
        frames: Int,
        hostFrames: Boolean,
        setup: (Int) -> Unit,
    ): List<Pair<Int, Int>> {
        val cached = texture(device)
        val reference = texture(device)
        val results = mutableListOf<Pair<Int, Int>>()
        BackendTexture.makeMetal(width, height, cached.objcPtr()).use { cachedBackend ->
            BackendTexture.makeMetal(width, height, reference.objcPtr()).use { referenceBackend ->
                val cachedSurface = assertNotNull(Surface.wrapBackendTexture(recorder, cachedBackend, ColorSpace.sRGB))
                val referenceSurface = assertNotNull(Surface.wrapBackendTexture(recorder, referenceBackend, ColorSpace.sRGB))
                for (frame in 0 until frames) {
                    if (hostFrames) RenderNodeCaches.beginFrame()
                    setup(frame)
                    cachedSurface.canvas.clear(Color.WHITE)
                    root.drawInto(cachedSurface.canvas)
                    RenderNodeCaches.drawWithoutCaches {
                        referenceSurface.canvas.clear(Color.WHITE)
                        root.drawInto(referenceSurface.canvas)
                    }
                    recorder.snap().use { recording -> context.insertRecording(recording) }
                    context.submit(syncCpu = true)
                    results += diff(bytesOf(cached), bytesOf(reference))
                    if (results.last().first > 0) println("  frame $frame differs within $lastBox")
                }
                cachedSurface.close()
                referenceSurface.close()
            }
        }
        return results
    }

    private fun Scene.card(x: Float, y: Float, index: Int, font: Font): RenderNode =
        node(Rect(x, y, x + 70.5f, y + 44.25f), {
            clip = true
            setClipRRect(0f, 0f, 70.5f, 44.25f, floatArrayOf(8f), antiAlias = true)
        }) {
            drawRRect(RRect.makeXYWH(0f, 0f, 70.5f, 44.25f, 8f), Paint().apply { color = Color.makeRGB(40 + index * 23 % 200, 80, 150) })
            drawCircle(12.25f, 12.75f, 7.1f, Paint().apply { color = Color.makeARGB(200, 255, 255, 255) })
            drawRect(Rect(24f, 6f, 64f, 10f), Paint().apply { color = Color.makeARGB(120, 0, 0, 0) })
            drawString("Card $index", 4.5f, 36.3f, font, Paint().apply { color = Color.WHITE })
            drawString("${index * 7}", 50.5f, 22.3f, font, Paint().apply { color = Color.makeARGB(180, 255, 230, 200) })
            drawString("x", 60.5f, 38.3f, font, Paint().apply { color = Color.makeARGB(160, 200, 255, 200) })
        }

    @Test
    fun texturesMatchDrawingWithoutThem() {
        if (hostOs == OS.Tvos) return
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val scene = Scene(RenderNodeContext(snapshotCache = true))
        val font = Font(null, 11f)
        val cards = (0 until 8).map { i -> scene.card(6f + (i % 4) * 76.5f, 8f + (i / 4) * 50.5f, i, font) }
        val grid = scene.node(Rect(4.5f, 4f, 314.5f, 116f), {
            clip = true
            setClipRRect(0f, 0f, 310f, 112f, floatArrayOf(12f), antiAlias = true)
        }) {
            drawRect(Rect(0f, 0f, 310f, 112f), Paint().apply { color = Color.makeRGB(24, 26, 33) })
            cards.forEach { it.drawInto(this) }
        }
        val page = scene.node(Rect(10.25f, 122f, 170.25f, 230f), { layerPaint = Paint().apply { setAlphaf(0.7f) } }) {
            drawRRect(RRect.makeXYWH(0f, 0f, 160f, 108f, 10f), Paint().apply { color = Color.makeRGB(70, 80, 100) })
            for (i in 0 until 3) drawString("Line $i of a page", 8.25f, 30.5f + i * 22f, font, Paint().apply { color = Color.WHITE })
        }
        val blurred = scene.node(Rect(0f, 0f, 120f, 90f), {
            layerPaint = Paint().apply { imageFilter = ImageFilter.makeBlur(8f, 8f, FilterTileMode.DECAL) }
        }) {
            drawCircle(60f, 45f, 30f, Paint().apply { color = Color.makeRGB(230, 120, 40) })
        }
        val panel = scene.node(Rect(185f, 130f, 305f, 220f), {
            clip = true
            setClipRRect(0f, 0f, 120f, 90f, floatArrayOf(14f), antiAlias = true)
        }) { blurred.drawInto(this) }
        val root = scene.node(Rect(0f, 0f, width.toFloat(), height.toFloat())) {
            drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            grid.drawInto(this)
            page.drawInto(this)
            panel.drawInto(this)
        }

        GraphiteContext.makeMetal(device.objcPtr(), queue.objcPtr()).use { context ->
            val recorder = context.makeRecorder()
            val results = frames(context, recorder, device, root, 6, hostFrames = false) {}
            results.forEachIndexed { frame, (differing, max) ->
                println("graphite frame $frame: cached vs without: $differing channels differ, max $max; ${RenderNodeCaches.count} textures")
            }
            val held = RenderNodeCaches.bytes
            println("graphite: ${RenderNodeCaches.count} textures, ${held / 1024} KB")
            assertTrue(held > 0, "the grid, the page and the panel keep textures")
            recorder.close()
            assertEquals(0L, RenderNodeCaches.bytes, "the recorder took its textures with it")
            val worst = results.maxOf { it.second }
            scene.close(); font.close()
            if (worst > 1) fail("textures differ from drawing without them by up to $worst")
        }
    }

    @Test
    fun promotedLayersComeToRestExactly() {
        if (hostOs == OS.Tvos) return
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val scene = Scene(RenderNodeContext(snapshotCache = true))
        val font = Font(null, 13f)
        val page = scene.node(Rect(20f, 20f, 200f, 140f), { layerPaint = Paint().apply { setAlphaf(0.9f) } }) {
            drawRRect(RRect.makeXYWH(0f, 0f, 180f, 120f, 12f), Paint().apply { color = Color.makeRGB(60, 90, 130) })
            for (i in 0 until 4) drawString("Line $i of a page", 10.5f, 24.5f + i * 22f, font, Paint().apply { color = Color.WHITE })
        }
        val root = scene.node(Rect(0f, 0f, width.toFloat(), height.toFloat())) {
            drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            page.drawInto(this)
        }
        // Scales in to 1, then up to rest at 1.05.
        val scales = listOf(0.94f, 0.95f, 0.962f, 0.975f, 0.986f, 1f, 1f, 1f, 1.01f, 1.03f, 1.05f, 1.05f, 1.05f)
        GraphiteContext.makeMetal(device.objcPtr(), queue.objcPtr()).use { context ->
            context.makeRecorder().use { recorder ->
                val results = frames(context, recorder, device, root, scales.size, hostFrames = true) { frame ->
                    page.scaleX = scales[frame]; page.scaleY = scales[frame]
                }
                var stillWorst = 0
                results.forEachIndexed { frame, (differing, max) ->
                    val still = frame >= 1 && scales[frame] == scales[frame - 1]
                    println("graphite promotion frame $frame (scale ${scales[frame]}${if (still) ", still" else ""}): $differing channels differ, max $max")
                    if (still) stillWorst = maxOf(stillWorst, max)
                }
                scene.close(); font.close()
                if (stillWorst > 0) fail("still frames differ from drawing without textures by up to $stillWorst")
            }
        }
        RenderNodeCaches.purge()
    }
}
