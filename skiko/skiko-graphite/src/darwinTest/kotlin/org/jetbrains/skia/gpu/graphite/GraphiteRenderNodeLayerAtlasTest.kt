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
 * Through Graphite, a tree drawn straight onto the canvas draws its alpha layers from the layer atlas, and drawn
 * through a picture each layer is a saveLayer; both have to produce the same pixels.
 */
@OptIn(ExperimentalForeignApi::class, ExperimentalSkikoApi::class)
class GraphiteRenderNodeLayerAtlasTest {
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
    fun atlasLayersMatchSaveLayer() {
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
        val directTexture = texture()
        val pictureTexture = texture()

        val nodeContext = RenderNodeContext(snapshotCache = true)
        val nodes = mutableListOf<RenderNode>()
        fun node(bounds: Rect, block: RenderNode.() -> Unit = {}, draw: Canvas.() -> Unit) = RenderNode(nodeContext).also {
            it.bounds = bounds
            it.block()
            val canvas = it.beginRecording()
            canvas.draw()
            it.endRecording()
            nodes += it
        }
        val font = Font(null, 11f)
        val cards = (0 until 18).map { i ->
            val x = 6f + (i % 6) * 38.5f
            val y = 6f + (i / 6) * 48.25f
            node(Rect(x, y, x + 34.3f, y + 41.7f), {
                layerPaint = Paint().apply { setAlphaf(0.3f + (i % 7) * 0.1f) }
                scaleX = 0.91f + (i % 4) * 0.033f
                scaleY = scaleX
            }) {
                drawRRect(RRect.makeXYWH(0f, 0f, 34.3f, 41.7f, 6.5f), Paint().apply { color = Color.makeRGB(40 + i * 13 % 200, 90, 160) })
                drawString("C$i", 4.5f, 30.3f, font, Paint().apply { color = Color.WHITE })
            }
        }
        val root = node(Rect(0f, 0f, width.toFloat(), height.toFloat())) {
            drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            cards.forEach { it.drawInto(this) }
        }

        GraphiteContext.makeMetal(device.objcPtr(), queue.objcPtr()).use { context ->
            context.makeRecorder().use { recorder ->
                fun render(texture: MTLTextureProtocol, draw: (Canvas) -> Unit) {
                    BackendTexture.makeMetal(width, height, texture.objcPtr()).use { backend ->
                        assertNotNull(Surface.wrapBackendTexture(recorder, backend, ColorSpace.sRGB)).use { surface ->
                            repeat(2) {
                                surface.canvas.clear(Color.WHITE)
                                surface.canvas.save()
                                surface.canvas.translate(1.3f, 0.7f)
                                draw(surface.canvas)
                                surface.canvas.restore()
                            }
                            recorder.snap().use { recording -> context.insertRecording(recording) }
                            context.submit(syncCpu = true)
                        }
                    }
                }
                render(directTexture) { root.drawInto(it) }
                val recorderPicture = PictureRecorder()
                root.drawInto(recorderPicture.beginRecording(Rect(0f, 0f, width.toFloat(), height.toFloat())))
                recorderPicture.finishRecordingAsPicture().use { picture -> render(pictureTexture) { it.drawPicture(picture) } }
            }
        }

        val a = bytesOf(directTexture)
        val b = bytesOf(pictureTexture)
        var differing = 0
        var maxDiff = 0
        for (i in a.indices) {
            val d = kotlin.math.abs((a[i].toInt() and 0xFF) - (b[i].toInt() and 0xFF))
            if (d != 0) differing++
            if (d > maxDiff) maxDiff = d
        }
        println("graphite layer atlas vs saveLayer: $differing of ${a.size} channels differ, max $maxDiff")
        nodes.forEach { it.close() }
        nodeContext.close()
        font.close()
        if (maxDiff > 2) fail("layer atlas differs from saveLayer by up to $maxDiff")
    }
}
