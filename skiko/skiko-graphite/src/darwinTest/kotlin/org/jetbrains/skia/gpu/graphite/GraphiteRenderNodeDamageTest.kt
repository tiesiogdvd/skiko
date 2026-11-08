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
import org.jetbrains.skiko.node.RenderNodeContext
import org.jetbrains.skiko.node.RenderNodeDamageTracker
import platform.Metal.MTLCreateSystemDefaultDevice
import platform.Metal.MTLPixelFormatBGRA8Unorm
import platform.Metal.MTLRegionMake2D
import platform.Metal.MTLStorageModeShared
import platform.Metal.MTLTextureDescriptor
import platform.Metal.MTLTextureProtocol
import platform.Metal.MTLTextureUsageRenderTarget
import platform.Metal.MTLTextureUsageShaderRead
import kotlin.test.Test
import kotlin.test.assertNotNull
import kotlin.test.fail

/**
 * Through Graphite, a frame drawn only inside its damage (a pass a damaged rect) over the previous frame's pixels has
 * to equal the same frame drawn whole: alpha layers drawn from the atlas, a moving node, a re-recorded label, a blur.
 */
@OptIn(ExperimentalForeignApi::class, ExperimentalSkikoApi::class)
class GraphiteRenderNodeDamageTest {
    private val width = 240
    private val height = 160

    private fun bytesOf(texture: MTLTextureProtocol): ByteArray {
        val bytes = ByteArray(width * height * 4)
        bytes.usePinned {
            texture.getBytes(it.addressOf(0), (width * 4).toULong(), MTLRegionMake2D(0u, 0u, width.toULong(), height.toULong()), 0u)
        }
        return bytes
    }

    @Test
    fun damageClippedFramesMatchFullFrames() {
        if (hostOs == OS.Tvos) return
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        fun texture(): MTLTextureProtocol {
            val descriptor = MTLTextureDescriptor.texture2DDescriptorWithPixelFormat(
                MTLPixelFormatBGRA8Unorm, width.toULong(), height.toULong(), false,
            )
            descriptor.usage = MTLTextureUsageShaderRead or MTLTextureUsageRenderTarget
            descriptor.storageMode = MTLStorageModeShared
            return checkNotNull(device.newTextureWithDescriptor(descriptor))
        }
        val damagedTexture = texture()
        val fullTexture = texture()

        val tracker = RenderNodeDamageTracker()
        val nodeContext = RenderNodeContext(snapshotCache = true)
        val nodes = mutableListOf<RenderNode>()
        fun RenderNode.record(draw: Canvas.() -> Unit) {
            val canvas = beginRecording()
            canvas.draw()
            endRecording()
        }
        fun node(bounds: Rect, block: RenderNode.() -> Unit = {}, draw: Canvas.() -> Unit) = RenderNode(nodeContext).also {
            it.bounds = bounds
            it.block()
            it.record(draw)
            nodes += it
        }
        val cards = (0 until 12).map { i ->
            val x = 6f + (i % 6) * 38.5f
            val y = 6f + (i / 6) * 48.25f
            node(Rect(x, y, x + 34.3f, y + 41.7f), {
                layerPaint = Paint().apply { setAlphaf(0.3f + (i % 7) * 0.1f) }
                scaleX = 0.91f + (i % 4) * 0.033f
                scaleY = scaleX
            }) {
                drawRRect(RRect.makeXYWH(0f, 0f, 34.3f, 41.7f, 6.5f), Paint().apply { color = Color.makeRGB(40 + i * 13 % 200, 90, 160) })
                drawCircle(9.5f, 9.25f, 4.6f, Paint().apply { color = Color.WHITE; isAntiAlias = true })
            }
        }
        val blurred = node(Rect(150f, 104f, 230f, 150f), {
            layerPaint = Paint().apply { imageFilter = ImageFilter.makeBlur(5f, 5f, FilterTileMode.DECAL) }
        }) { drawOval(Rect(10f, 8f, 70f, 38f), Paint().apply { color = Color.makeRGB(230, 120, 40) }) }
        val mover = node(Rect(10f, 110f, 50f, 140f)) {
            drawRRect(RRect.makeXYWH(0f, 0f, 40f, 30f, 9f), Paint().apply { color = Color.CYAN; isAntiAlias = true })
        }
        var labelColor = Color.WHITE
        val root = node(Rect(0f, 0f, width.toFloat(), height.toFloat())) {}
        fun recordRoot() = root.record {
            drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            cards.forEach { it.drawInto(this) }
            drawRRect(RRect.makeXYWH(80.5f, 120.25f, 30f, 11f, 3f), Paint().apply { color = labelColor; isAntiAlias = true })
            mover.drawInto(this)
            blurred.drawInto(this)
        }
        recordRoot()

        val changes = listOf<() -> Unit>(
            {},
            { mover.translationX = 17.5f },
            { cards[3].layerPaint = Paint().apply { setAlphaf(0.9f) } },
            { labelColor = Color.YELLOW; recordRoot() },
            { blurred.record { drawOval(Rect(14f, 8f, 66f, 38f), Paint().apply { color = Color.GREEN }) } },
            { mover.translationY = -20f; cards[7].scaleX = 1f },
            {},
        )
        var worst = 0
        GraphiteContext.makeMetal(device.objcPtr(), queue.objcPtr()).use { context ->
            context.makeRecorder().use { recorder ->
                BackendTexture.makeMetal(width, height, damagedTexture.objcPtr()).use { damagedBackend ->
                    BackendTexture.makeMetal(width, height, fullTexture.objcPtr()).use { fullBackend ->
                        val damaged = assertNotNull(Surface.wrapBackendTexture(recorder, damagedBackend, ColorSpace.sRGB))
                        val full = assertNotNull(Surface.wrapBackendTexture(recorder, fullBackend, ColorSpace.sRGB))
                        for ((frame, change) in changes.withIndex()) {
                            change()
                            tracker.beginFrame(width, height)
                            tracker.addNode(root)
                            val rects = tracker.endFrame()
                            val canvas = damaged.canvas
                            val passes = if (rects == null) listOf(Rect.makeWH(width.toFloat(), height.toFloat()))
                            else (rects.indices step 4).map { Rect.makeLTRB(rects[it].toFloat(), rects[it + 1].toFloat(), rects[it + 2].toFloat(), rects[it + 3].toFloat()) }
                            for (pass in passes) {
                                val saved = canvas.save()
                                canvas.clipRect(pass)
                                canvas.clear(Color.WHITE)
                                root.drawInto(canvas)
                                canvas.restoreToCount(saved)
                            }
                            full.canvas.clear(Color.WHITE)
                            root.drawInto(full.canvas)
                            recorder.snap().use { recording -> context.insertRecording(recording) }
                            context.submit(syncCpu = true)

                            val a = bytesOf(damagedTexture)
                            val b = bytesOf(fullTexture)
                            var differing = 0
                            var maxDiff = 0
                            for (i in a.indices) {
                                val d = kotlin.math.abs((a[i].toInt() and 0xFF) - (b[i].toInt() and 0xFF))
                                if (d != 0) differing++
                                if (d > maxDiff) maxDiff = d
                            }
                            println("graphite frame $frame: damage ${rects?.toList() ?: "full"}; $differing channels differ, max $maxDiff")
                            worst = maxOf(worst, maxDiff)
                        }
                        damaged.close()
                        full.close()
                    }
                }
            }
        }
        tracker.close()
        nodes.forEach { it.close() }
        nodeContext.close()
        if (worst > 0) fail("damage-clipped Graphite frames differ from full frames by up to $worst")
    }
}
