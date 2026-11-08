package org.jetbrains.skiko.node

import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.objcPtr
import org.jetbrains.skia.*
import platform.Metal.MTLCreateSystemDefaultDevice
import kotlin.test.Test
import kotlin.test.assertTrue
import kotlin.test.fail

/**
 * A tree drawn straight onto a GPU canvas draws its layers from the frame's layer atlas; drawn through a picture,
 * every layer is the saveLayer it records. Both have to produce the same pixels.
 */
@OptIn(ExperimentalForeignApi::class)
class RenderNodeLayerAtlasTest {
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

    /** Cards like the UI perf demo's: rounded fill, a smaller shape over it, text, under alpha and a fractional scale. */
    private fun Scene.card(x: Float, y: Float, index: Int, alpha: Float, scale: Float, font: Font): RenderNode =
        node(Rect(x, y, x + 61.3f, y + 47.7f), {
            layerPaint = Paint().apply { setAlphaf(alpha) }
            this.alpha = alpha
            scaleX = scale
            scaleY = scale
            translationY = (index % 5) * 0.37f
        }) {
            drawRRect(RRect.makeXYWH(0f, 0f, 61.3f, 47.7f, 7.5f), Paint().apply { color = Color.makeRGB(40 + index * 13 % 200, 90, 160) })
            drawCircle(12.25f, 12.75f, 6.1f, Paint().apply { color = Color.WHITE })
            drawString("Card $index", 4.5f, 38.3f, font, Paint().apply { color = Color.WHITE })
        }

    private fun Scene.build(font: Font): RenderNode {
        val cards = (0 until 24).map { i ->
            card(8f + (i % 6) * 50.5f, 6f + (i / 6) * 56.25f, i, alpha = 0.3f + (i % 7) * 0.1f, scale = 0.91f + (i % 4) * 0.033f, font)
        }
        // A grid clipped to rounded bounds, holding the cards.
        val grid = node(Rect(4f, 4f, 316f, 200f), {
            clip = true
            setClipRRect(0f, 0f, 312f, 196f, floatArrayOf(14f), antiAlias = true)
        }) {
            drawRect(Rect(0f, 0f, 312f, 196f), Paint().apply { color = Color.makeRGB(21, 23, 28) })
            cards.forEach { it.drawInto(this) }
        }
        // A layer with a color filter, one with a blend mode, an offscreen one at alpha 1, a rotated one,
        // and one holding a nested layer (left to saveLayer).
        val tinted = node(Rect(10f, 205f, 60f, 235f), {
            layerPaint = Paint().apply { colorFilter = ColorFilter.makeBlend(Color.makeARGB(128, 255, 0, 0), BlendMode.SRC_ATOP) }
        }) {
            drawOval(Rect(0f, 0f, 50f, 30f), Paint().apply { color = Color.GREEN })
        }
        val multiplied = node(Rect(70f, 200f, 130f, 238f), {
            layerPaint = Paint().apply { blendMode = BlendMode.MULTIPLY; setAlphaf(0.8f) }
        }) {
            drawRRect(RRect.makeXYWH(0f, 0f, 60f, 38f, 9f), Paint().apply { color = Color.makeRGB(250, 200, 30) })
        }
        val offscreen = node(Rect(140f, 205f, 180f, 235f), { layerPaint = Paint() }) {
            drawRect(Rect(0f, 0f, 40f, 30f), Paint().apply { color = Color.makeARGB(160, 0, 128, 255) })
            drawRect(Rect(10f, 5f, 50f, 25f), Paint().apply { color = Color.makeARGB(160, 255, 128, 0) })
        }
        val rotated = node(Rect(190f, 200f, 240f, 235f), {
            layerPaint = Paint().apply { setAlphaf(0.6f) }
            rotationZ = 17.3f
        }) {
            drawRect(Rect(0f, 0f, 50f, 35f), Paint().apply { color = Color.CYAN })
            drawString("rot", 5f, 25f, font, Paint().apply { color = Color.BLACK })
        }
        val inner = node(Rect(5f, 5f, 30f, 25f), { layerPaint = Paint().apply { setAlphaf(0.5f) } }) {
            drawRect(Rect(0f, 0f, 25f, 20f), Paint().apply { color = Color.RED })
        }
        val outer = node(Rect(250f, 200f, 300f, 238f), { layerPaint = Paint().apply { setAlphaf(0.7f) } }) {
            drawRect(Rect(0f, 0f, 50f, 38f), Paint().apply { color = Color.BLUE })
            inner.drawInto(this)
        }
        return node(Rect(0f, 0f, width.toFloat(), height.toFloat())) {
            drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            grid.drawInto(this)
            tinted.drawInto(this)
            multiplied.drawInto(this)
            offscreen.drawInto(this)
            rotated.drawInto(this)
            outer.drawInto(this)
        }
    }

    private fun pixels(surface: Surface): ByteArray {
        val bitmap = Bitmap()
        bitmap.allocPixels(ImageInfo.makeN32Premul(width, height))
        assertTrue(surface.readPixels(bitmap, 0, 0))
        return bitmap.readPixels()!!
    }

    private fun compare(snapshotCache: Boolean, canvasScale: Float, canvasTranslate: Float) {
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val gpu = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr())
        val scene = Scene(RenderNodeContext(snapshotCache = snapshotCache))
        val font = Font(null, 11f)
        val root = scene.build(font)

        fun makeSurface() = Surface.makeRenderTarget(
            gpu, false, ImageInfo.makeN32Premul(width, height), 0, SurfaceOrigin.TOP_LEFT, null, false
        )
        fun Canvas.prepare() {
            clear(Color.WHITE)
            translate(canvasTranslate, canvasTranslate)
            scale(canvasScale, canvasScale)
        }

        // Twice each, so that the second frame draws from the snapshots the first one took.
        val direct = makeSurface()
        repeat(2) {
            direct.canvas.save(); direct.canvas.prepare(); root.drawInto(direct.canvas); direct.canvas.restore()
        }
        val recorder = PictureRecorder()
        root.drawInto(recorder.beginRecording(Rect(0f, 0f, width.toFloat(), height.toFloat())))
        val picture = recorder.finishRecordingAsPicture()
        val viaPicture = makeSurface()
        repeat(2) {
            viaPicture.canvas.save(); viaPicture.canvas.prepare(); viaPicture.canvas.drawPicture(picture); viaPicture.canvas.restore()
        }
        gpu.flush(); gpu.submit(syncCpu = true)

        val a = pixels(direct)
        val b = pixels(viaPicture)
        var differing = 0
        var maxDiff = 0
        for (i in a.indices) {
            val d = kotlin.math.abs((a[i].toInt() and 0xFF) - (b[i].toInt() and 0xFF))
            if (d != 0) differing++
            if (d > maxDiff) maxDiff = d
        }
        println("layer atlas vs saveLayer (snapshotCache=$snapshotCache, scale=$canvasScale): $differing of ${a.size} channels differ, max $maxDiff")

        picture.close(); direct.close(); viaPicture.close(); font.close(); scene.close(); gpu.close()
        if (maxDiff > 2) fail("layer atlas differs from saveLayer by up to $maxDiff")
    }

    @Test
    fun retainedLayersMatchSaveLayerAcrossFrames() {
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val gpu = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr())
        val scene = Scene(RenderNodeContext(snapshotCache = true))
        val font = Font(null, 11f)
        // A static page fading in, holding a node that changes on one frame; a blurred panel over
        // static content, inside a rounded clip; and a card whose scale animates (never retained).
        var badgeColor = Color.RED
        val badge = scene.node(Rect(10f, 10f, 40f, 30f)) { drawRect(Rect(0f, 0f, 30f, 20f), Paint().apply { color = Color.RED }) }
        val page = scene.node(Rect(12.5f, 8f, 172.5f, 148f), { layerPaint = Paint().apply { setAlphaf(0.1f) } }) {
            drawRRect(RRect.makeXYWH(0f, 0f, 160f, 140f, 12f), Paint().apply { color = Color.makeRGB(60, 70, 90) })
            drawString("Static page", 8.25f, 60.5f, font, Paint().apply { color = Color.WHITE })
            badge.drawInto(this)
        }
        val blurred = scene.node(Rect(0f, 0f, 110f, 80f), {
            layerPaint = Paint().apply { imageFilter = ImageFilter.makeBlur(9f, 9f, FilterTileMode.DECAL) }
        }) {
            drawCircle(55f, 40f, 30f, Paint().apply { color = Color.makeRGB(230, 120, 40) })
            drawString("Blur", 30f, 45f, font, Paint().apply { color = Color.WHITE })
        }
        val panel = scene.node(Rect(190f, 20f, 300f, 100f), {
            clip = true
            setClipRRect(0f, 0f, 110f, 80f, floatArrayOf(16f), antiAlias = true)
        }) { blurred.drawInto(this) }
        val card = scene.card(200f, 140f, 3, alpha = 0.6f, scale = 1f, font)
        val root = scene.node(Rect(0f, 0f, width.toFloat(), height.toFloat())) {
            drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            page.drawInto(this)
            panel.drawInto(this)
            card.drawInto(this)
        }

        fun makeSurface() = Surface.makeRenderTarget(
            gpu, false, ImageInfo.makeN32Premul(width, height), 0, SurfaceOrigin.TOP_LEFT, null, false
        )
        val direct = makeSurface()
        val viaPicture = makeSurface()
        var worst = 0
        for (frame in 0 until 6) {
            page.layerPaint = Paint().apply { setAlphaf(0.1f + frame * 0.15f) }
            card.scaleX = 0.9f + frame * 0.021f
            card.scaleY = card.scaleX
            if (frame == 3) {
                badgeColor = Color.GREEN
                badge.beginRecording().drawRect(Rect(0f, 0f, 30f, 20f), Paint().apply { color = badgeColor })
                badge.endRecording()
            }
            direct.canvas.clear(Color.WHITE)
            root.drawInto(direct.canvas)
            val recorder = PictureRecorder()
            root.drawInto(recorder.beginRecording(Rect(0f, 0f, width.toFloat(), height.toFloat())))
            val picture = recorder.finishRecordingAsPicture()
            viaPicture.canvas.clear(Color.WHITE)
            viaPicture.canvas.drawPicture(picture)
            gpu.flush(); gpu.submit(syncCpu = true)
            picture.close()

            val a = pixels(direct)
            val b = pixels(viaPicture)
            var differing = 0
            var maxDiff = 0
            var box = IntArray(4) { if (it < 2) Int.MAX_VALUE else -1 }
            for (i in a.indices) {
                val d = kotlin.math.abs((a[i].toInt() and 0xFF) - (b[i].toInt() and 0xFF))
                if (d != 0) {
                    differing++
                    val x = (i / 4) % width; val y = (i / 4) / width
                    box = intArrayOf(minOf(box[0], x), minOf(box[1], y), maxOf(box[2], x), maxOf(box[3], y))
                }
                if (d > maxDiff) maxDiff = d
            }
            println("frame $frame: retained/atlas vs saveLayer: $differing of ${a.size} channels differ, max $maxDiff, within ${box.toList()}")
            worst = maxOf(worst, maxDiff)
        }
        direct.close(); viaPicture.close(); font.close(); scene.close(); gpu.close()
        if (worst > 2) fail("retained layers differ from saveLayer by up to $worst")
    }

    @Test
    fun atlasLayersMatchSaveLayer() = compare(snapshotCache = true, canvasScale = 1f, canvasTranslate = 0f)

    @Test
    fun atlasLayersMatchSaveLayerWithoutSnapshots() = compare(snapshotCache = false, canvasScale = 1f, canvasTranslate = 0f)

    @Test
    fun atlasLayersMatchSaveLayerUnderAFractionalTransform() = compare(snapshotCache = true, canvasScale = 0.9375f, canvasTranslate = 3.3f)
}
