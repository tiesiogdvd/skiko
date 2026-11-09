package org.jetbrains.skiko.node

import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.objcPtr
import org.jetbrains.skia.*
import platform.Metal.MTLCreateSystemDefaultDevice
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue
import kotlin.test.fail

/**
 * Nodes drawn from the textures they keep (retained layers, the raster cache, promotions) against the same nodes drawn
 * without them ([RenderNodeCaches.drawWithoutCaches]), frame after frame, on Ganesh (Metal). At rest a layer's texture
 * reproduces saveLayer and a content texture the direct drawing within 1/255; while a promotion moves, the composite
 * is filtered; once the motion stops the nodes draw exactly again.
 */
@OptIn(ExperimentalForeignApi::class)
class RenderNodeRasterCacheTest {
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

    private class Gpu(val context: DirectContext, val width: Int, val height: Int) {
        fun surface() = Surface.makeRenderTarget(
            context, false, ImageInfo.makeN32Premul(width, height), 0, SurfaceOrigin.TOP_LEFT, null, false
        )

        fun pixels(surface: Surface): ByteArray {
            context.flush(); context.submit(syncCpu = true)
            val bitmap = Bitmap()
            bitmap.allocPixels(ImageInfo.makeN32Premul(width, height))
            assertTrue(surface.readPixels(bitmap, 0, 0))
            return bitmap.readPixels()!!.also { bitmap.close() }
        }
    }

    private fun withGpu(block: (Gpu) -> Unit) {
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val context = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr())
        try {
            block(Gpu(context, width, height))
        } finally {
            RenderNodeCaches.purge()
            context.close()
        }
    }

    /** The channels that differ, the largest difference, and where. */
    private class Diff(val differing: Int, val max: Int, val box: List<Int>) {
        override fun toString() = "$differing channels differ, max $max" + if (differing > 0) ", within $box" else ""
    }

    private fun diff(a: ByteArray, b: ByteArray): Diff {
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
        return Diff(differing, max, box.toList())
    }

    /** A card: a rounded fill, a circle, a label, clipped to its rounded bounds. */
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
    fun texturesAtRestMatchDrawingWithoutThem() = withGpu { gpu ->
        val scene = Scene(RenderNodeContext(snapshotCache = true))
        val font = Font(null, 11f)
        // A clipped grid of cards (content textures), a page under alpha (a retained layer), a blurred panel (its
        // filtered output), and a badge that changes on frame 4 inside the grid.
        var badgeColor = Color.RED
        val badge = scene.node(Rect(150f, 4f, 170f, 16f)) { drawRect(Rect(0f, 0f, 20f, 12f), Paint().apply { color = Color.RED }) }
        val cards = (0 until 8).map { i -> scene.card(6f + (i % 4) * 76.5f, 22f + (i / 4) * 50.5f, i, font) }
        val grid = scene.node(Rect(4.5f, 4f, 314.5f, 130f), {
            clip = true
            setClipRRect(0f, 0f, 310f, 126f, floatArrayOf(12f), antiAlias = true)
        }) {
            drawRect(Rect(0f, 0f, 310f, 126f), Paint().apply { color = Color.makeRGB(24, 26, 33) })
            cards.forEach { it.drawInto(this) }
            badge.drawInto(this)
        }
        val page = scene.node(Rect(10.25f, 136f, 170.25f, 230f), { layerPaint = Paint().apply { setAlphaf(0.7f) } }) {
            drawRRect(RRect.makeXYWH(0f, 0f, 160f, 94f, 10f), Paint().apply { color = Color.makeRGB(70, 80, 100) })
            drawString("Static page", 8.25f, 40.5f, font, Paint().apply { color = Color.WHITE })
        }
        val blurred = scene.node(Rect(0f, 0f, 120f, 90f), {
            layerPaint = Paint().apply { imageFilter = ImageFilter.makeBlur(8f, 8f, FilterTileMode.DECAL) }
        }) {
            drawCircle(60f, 45f, 30f, Paint().apply { color = Color.makeRGB(230, 120, 40) })
        }
        val panel = scene.node(Rect(185f, 138f, 305f, 228f), {
            clip = true
            setClipRRect(0f, 0f, 120f, 90f, floatArrayOf(14f), antiAlias = true)
        }) { blurred.drawInto(this) }
        val root = scene.node(Rect(0f, 0f, width.toFloat(), height.toFloat())) {
            drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            grid.drawInto(this)
            page.drawInto(this)
            panel.drawInto(this)
        }

        val cached = gpu.surface()
        val reference = gpu.surface()
        var worst = 0
        var maxTextures = 0
        for (frame in 0 until 8) {
            if (frame == 4) {
                badgeColor = Color.GREEN
                badge.beginRecording().drawRect(Rect(0f, 0f, 20f, 12f), Paint().apply { color = badgeColor })
                badge.endRecording()
            }
            cached.canvas.clear(Color.WHITE)
            root.drawInto(cached.canvas)
            RenderNodeCaches.drawWithoutCaches {
                reference.canvas.clear(Color.WHITE)
                root.drawInto(reference.canvas)
            }
            val d = diff(gpu.pixels(cached), gpu.pixels(reference))
            maxTextures = maxOf(maxTextures, RenderNodeCaches.count)
            println("ganesh frame $frame: cached vs without: $d; ${RenderNodeCaches.count} textures, ${RenderNodeCaches.bytes / 1024} KB")
            worst = maxOf(worst, d.max)
        }
        assertTrue(maxTextures >= 3, "the grid, the page and the panel keep textures")
        assertTrue(RenderNodeCaches.bytes > 0)
        RenderNodeCaches.purge()
        assertEquals(0L, RenderNodeCaches.bytes, "purge frees this thread's textures")
        cached.close(); reference.close(); font.close(); scene.close()
        if (worst > 1) fail("textures differ from drawing without them by up to $worst")
    }

    @Test
    fun scrollingByWholePixelsDrawsTheSameTextures() = withGpu { gpu ->
        val scene = Scene(RenderNodeContext(snapshotCache = true))
        val font = Font(null, 11f)
        val cards = (0 until 12).map { i -> scene.card(8f, 6f + i * 50f, i, font) }
        val content = scene.node(Rect(0f, 0f, 100f, 600f)) { cards.forEach { it.drawInto(this) } }
        val list = scene.node(Rect(20f, 10f, 120f, 230f), { clip = true }) { content.drawInto(this) }
        val root = scene.node(Rect(0f, 0f, width.toFloat(), height.toFloat())) {
            drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            list.drawInto(this)
        }
        val cached = gpu.surface()
        val reference = gpu.surface()
        var worst = 0
        val counts = mutableListOf<Int>()
        for (frame in 0 until 12) {
            content.translationY = -frame * 7f
            cached.canvas.clear(Color.WHITE)
            root.drawInto(cached.canvas)
            RenderNodeCaches.drawWithoutCaches {
                reference.canvas.clear(Color.WHITE)
                root.drawInto(reference.canvas)
            }
            val d = diff(gpu.pixels(cached), gpu.pixels(reference))
            counts += RenderNodeCaches.count
            println("ganesh scroll frame $frame: cached vs without: $d; ${RenderNodeCaches.count} textures")
            worst = maxOf(worst, d.max)
        }
        assertTrue(counts.last() > 0, "the cards keep textures while they scroll")
        cached.close(); reference.close(); font.close(); scene.close()
        if (worst > 1) fail("scrolled textures differ from drawing without them by up to $worst")
    }

    /**
     * A host's frames (beginFrame, and a settle frame while settleRequested): a page scaling in under alpha, then one
     * scaling up to rest at 1.05, composited from promoted textures while they move, and exact again once still.
     */
    @Test
    fun promotedLayersComeToRestExactly() = withGpu { gpu ->
        val scene = Scene(RenderNodeContext(snapshotCache = true))
        val font = Font(null, 13f)
        val page = scene.node(Rect(20f, 20f, 200f, 140f), { layerPaint = Paint().apply { setAlphaf(0.9f) } }) {
            drawRRect(RRect.makeXYWH(0f, 0f, 180f, 120f, 12f), Paint().apply { color = Color.makeRGB(60, 90, 130) })
            for (i in 0 until 4) drawString("Line $i of a page", 10.5f, 24.5f + i * 22f, font, Paint().apply { color = Color.WHITE })
        }
        val button = scene.node(Rect(220f, 160f, 300f, 200f), {
            clip = true
            setClipRRect(0f, 0f, 80f, 40f, floatArrayOf(20f), antiAlias = true)
        }) {
            drawRect(Rect(0f, 0f, 80f, 40f), Paint().apply { color = Color.makeRGB(200, 90, 60) })
            drawString("Hover", 18.5f, 25.5f, font, Paint().apply { color = Color.WHITE })
            drawCircle(10f, 20f, 4f, Paint().apply { color = Color.WHITE })
            drawCircle(70f, 20f, 4f, Paint().apply { color = Color.WHITE })
            drawRect(Rect(10f, 32f, 70f, 34f), Paint().apply { color = Color.makeARGB(140, 255, 255, 255) })
            drawRect(Rect(10f, 4f, 70f, 6f), Paint().apply { color = Color.makeARGB(140, 255, 255, 255) })
            drawString("me", 50.5f, 14.5f, font, Paint().apply { color = Color.makeARGB(200, 255, 255, 255) })
        }
        val root = scene.node(Rect(0f, 0f, width.toFloat(), height.toFloat())) {
            drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            page.drawInto(this)
            button.drawInto(this)
        }
        val cached = gpu.surface()
        val reference = gpu.surface()
        val pageScales = listOf(0.94f, 0.95f, 0.962f, 0.975f, 0.986f, 0.995f, 1f, 1f, 1f, 1f, 1f, 1f)
        val buttonScales = listOf(1f, 1f, 1f, 1f, 1.01f, 1.022f, 1.035f, 1.044f, 1.05f, 1.05f, 1.05f, 1.05f)
        var settledWorst = 0
        var movingWorst = 0
        for (frame in pageScales.indices) {
            RenderNodeCaches.beginFrame()
            page.scaleX = pageScales[frame]; page.scaleY = pageScales[frame]
            button.scaleX = buttonScales[frame]; button.scaleY = buttonScales[frame]
            cached.canvas.clear(Color.WHITE)
            root.drawInto(cached.canvas)
            val settle = RenderNodeCaches.settleRequested
            RenderNodeCaches.drawWithoutCaches {
                reference.canvas.clear(Color.WHITE)
                root.drawInto(reference.canvas)
            }
            val d = diff(gpu.pixels(cached), gpu.pixels(reference))
            val still = frame >= 1 && pageScales[frame] == pageScales[frame - 1] && buttonScales[frame] == buttonScales[frame - 1]
            println("ganesh promotion frame $frame (page ${pageScales[frame]}, button ${buttonScales[frame]}): $d; settle requested $settle")
            if (still) settledWorst = maxOf(settledWorst, d.max) else movingWorst = maxOf(movingWorst, d.max)
            if (frame == pageScales.lastIndex) assertTrue(!settle, "nothing to settle once everything is still")
        }
        println("ganesh promotion: moving frames differ by up to $movingWorst, still frames by up to $settledWorst")
        cached.close(); reference.close(); font.close(); scene.close()
        if (settledWorst > 1) fail("still frames differ from drawing without textures by up to $settledWorst")
    }
}
