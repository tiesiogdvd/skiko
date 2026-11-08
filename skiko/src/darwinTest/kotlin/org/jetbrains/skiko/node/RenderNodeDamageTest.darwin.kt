package org.jetbrains.skiko.node

import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.objcPtr
import org.jetbrains.skia.*
import platform.Metal.MTLCreateSystemDefaultDevice
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue
import kotlin.test.fail

/**
 * A frame drawn only inside its damage, over the previous frame's pixels, has to equal the same frame drawn whole.
 * Each frame changes something else: an alpha, a translation, a re-recorded node inside a layer, a blurred panel's
 * content, a child added and removed (its parent recorded anew), a shadow's elevation, a node drawn twice.
 */
@OptIn(ExperimentalForeignApi::class)
class RenderNodeDamageTest {
    private val width = 320
    private val height = 240

    private class Scene(val context: RenderNodeContext) {
        val nodes = mutableListOf<RenderNode>()

        fun node(bounds: Rect, block: RenderNode.() -> Unit = {}, draw: Canvas.() -> Unit): RenderNode {
            val node = RenderNode(context)
            node.bounds = bounds
            node.block()
            node.record(draw)
            nodes += node
            return node
        }

        fun close() {
            nodes.forEach { it.close() }
            context.close()
        }
    }

    private fun pixels(surface: Surface): ByteArray {
        val bitmap = Bitmap()
        bitmap.allocPixels(ImageInfo.makeN32Premul(width, height))
        assertTrue(surface.readPixels(bitmap, 0, 0))
        return bitmap.readPixels()!!
    }

    private fun area(rects: IntArray?): Int {
        if (rects == null) return width * height
        var sum = 0
        for (i in rects.indices step 4) sum += (rects[i + 2] - rects[i]) * (rects[i + 3] - rects[i + 1])
        return sum
    }

    @Test
    fun damageClippedFramesMatchFullFrames() {
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val gpu = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr())
        val context = RenderNodeContext(snapshotCache = true).apply {
            damageTracking = true
            setLightingInfo(centerX = 160f, centerY = -100f, centerZ = 600f, radius = 800f, ambientShadowAlpha = 0.04f, spotShadowAlpha = 0.19f)
        }
        val scene = Scene(context)
        val font = Font(null, 11f)

        val badge = scene.node(Rect(10f, 10f, 40f, 30f)) { drawRect(Rect(0f, 0f, 30f, 20f), Paint().apply { color = Color.RED }) }
        val page = scene.node(Rect(12.5f, 8f, 172.5f, 148f), { layerPaint = Paint().apply { setAlphaf(0.8f) } }) {
            drawRRect(RRect.makeXYWH(0f, 0f, 160f, 140f, 12f), Paint().apply { color = Color.makeRGB(60, 70, 90) })
            drawString("Static page", 8.25f, 60.5f, font, Paint().apply { color = Color.WHITE })
            badge.drawInto(this)
        }
        var blurColor = Color.makeRGB(230, 120, 40)
        val blurred = scene.node(Rect(0f, 0f, 110f, 80f), {
            layerPaint = Paint().apply { imageFilter = ImageFilter.makeBlur(6f, 6f, FilterTileMode.DECAL) }
        }) {
            drawCircle(55f, 40f, 30f, Paint().apply { color = blurColor })
        }
        val panel = scene.node(Rect(190f, 20f, 300f, 100f), {
            clip = true
            setClipRRect(0f, 0f, 110f, 80f, floatArrayOf(16f), antiAlias = true)
        }) { blurred.drawInto(this) }
        val mover = scene.node(Rect(20f, 160f, 60f, 190f)) {
            drawOval(Rect(0f, 0f, 40f, 30f), Paint().apply { color = Color.CYAN; isAntiAlias = true })
            drawString("move", 4f, 19f, font, Paint().apply { color = Color.BLACK })
        }
        val fader = scene.node(Rect(200f, 120f, 260f, 160f), { alpha = 0.5f }) {
            drawRect(Rect(0f, 0f, 60f, 40f), Paint().apply { color = Color.MAGENTA })
            drawRect(Rect(10f, 10f, 50f, 30f), Paint().apply { color = Color.YELLOW })
        }
        val raised = scene.node(Rect(100f, 170f, 160f, 220f), {
            clip = true
            setClipRRect(0f, 0f, 60f, 50f, floatArrayOf(8f), antiAlias = true)
            shadowElevation = 4f
        }) { drawRect(Rect(0f, 0f, 60f, 50f), Paint().apply { color = Color.WHITE }) }
        val extra = scene.node(Rect(0f, 0f, 30f, 20f)) { drawRect(Rect(0f, 0f, 30f, 20f), Paint().apply { color = Color.GREEN }) }
        val twice = scene.node(Rect(0f, 0f, 20f, 12f)) { drawRect(Rect(0f, 0f, 20f, 12f), Paint().apply { color = Color.BLUE }) }
        var withExtra = false
        val holder = scene.node(Rect(250f, 180f, 310f, 235f)) {}
        fun recordHolder() = holder.record {
            drawRect(Rect(0f, 0f, 60f, 55f), Paint().apply { color = Color.makeRGB(40, 40, 40) })
            if (withExtra) { save(); translate(15f, 5f); extra.drawInto(this); restore() }
            twice.drawInto(this)
            save(); translate(30f, 35f); twice.drawInto(this); restore()
        }
        recordHolder()
        // The root draws a label of its own, as a page drawing text without a layer does (a shape: the test's
        // default typeface may have no glyphs).
        var labelColor = Color.WHITE
        val root = scene.node(Rect(0f, 0f, width.toFloat(), height.toFloat())) {}
        fun recordRoot() = root.record {
            drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            page.drawInto(this)
            panel.drawInto(this)
            drawRRect(RRect.makeXYWH(180.5f, 214.25f, 24f, 11f, 3f), Paint().apply { color = labelColor; isAntiAlias = true })
            mover.drawInto(this)
            fader.drawInto(this)
            raised.drawInto(this)
            holder.drawInto(this)
        }
        recordRoot()

        fun makeSurface() = Surface.makeRenderTarget(
            gpu, false, ImageInfo.makeN32Premul(width, height), 0, SurfaceOrigin.TOP_LEFT, null, false
        )
        val damaged = makeSurface()
        val full = makeSurface()
        val tracker = RenderNodeDamageTracker()
        val region = Region()
        var outsideBlur = 0
        val changes = listOf<() -> Unit>(
            {},
            {},
            { mover.translationX = 37.5f },
            { fader.alpha = 0.8f },
            { badge.record { drawRect(Rect(0f, 0f, 30f, 20f), Paint().apply { color = Color.GREEN }) } },
            { blurColor = Color.makeRGB(40, 200, 90); blurred.record { drawCircle(55f, 40f, 30f, Paint().apply { color = blurColor }) } },
            { withExtra = true; recordHolder() },
            { withExtra = false; recordHolder() },
            { raised.shadowElevation = 10f },
            { twice.record { drawRect(Rect(0f, 0f, 20f, 12f), Paint().apply { color = Color.WHITE }) } },
            { page.layerPaint = Paint().apply { setAlphaf(0.5f) } },
            { mover.translationY = -3.25f; fader.alpha = 0.3f },
            { labelColor = Color.makeRGB(255, 200, 120); recordRoot() },
            { recordRoot() },
            {},
        )
        for ((frame, change) in changes.withIndex()) {
            change()
            tracker.beginFrame(width, height)
            tracker.addNode(root)
            val rects = tracker.endFrame()
            if (frame == 0) assertNull(rects, "the first frame is damaged whole")
            if (frame == 1 || frame == changes.lastIndex) assertEquals(0, rects?.size, "an unchanged frame has no damage")
            if (frame == 2) assertTrue(area(rects) < width * height / 8, "a moved node damages about its old and new bounds")
            if (frame == changes.lastIndex - 2) assertTrue(area(rects) < width * height / 20, "a changed label damages about its bounds: ${rects?.toList()}")
            if (frame == changes.lastIndex - 1) assertEquals(0, rects?.size, "the same content recorded anew damages nothing")

            val canvas = damaged.canvas
            val count = canvas.save()
            if (rects != null) {
                region.setEmpty()
                for (i in rects.indices step 4) region.op(rects[i], rects[i + 1], rects[i + 2], rects[i + 3], Region.Op.UNION)
                canvas.clipRegion(region)
            }
            if (rects == null || rects.isNotEmpty()) {
                canvas.clear(Color.WHITE)
                root.drawInto(canvas)
            }
            canvas.restoreToCount(count)

            full.canvas.clear(Color.WHITE)
            root.drawInto(full.canvas)
            gpu.flush(); gpu.submit(syncCpu = true)

            val a = pixels(damaged)
            val b = pixels(full)
            var differing = 0
            var maxDiff = 0
            var box = IntArray(4) { if (it < 2) Int.MAX_VALUE else -1 }
            for (i in a.indices) {
                val d = kotlin.math.abs((a[i].toInt() and 0xFF) - (b[i].toInt() and 0xFF))
                if (d != 0) {
                    differing++
                    val x = (i / 4) % width; val y = (i / 4) / width
                    box = intArrayOf(minOf(box[0], x), minOf(box[1], y), maxOf(box[2], x), maxOf(box[3], y))
                    // A retained blur differs from its saveLayer by up to 1 (it stores the filter's output in 8 bits
                    // before compositing it), and the damage-clipped buffer keeps whichever drew last: only there.
                    if (x !in 190 until 300 || y !in 20 until 100 || d > 1) outsideBlur = maxOf(outsideBlur, d)
                }
                if (d > maxDiff) maxDiff = d
            }
            println(
                "frame $frame: damage ${rects?.toList() ?: "full"}, ${area(rects) * 100 / (width * height)}% of the target, " +
                    "walked ${tracker.walkedNodes} (${tracker.skippedNodes} unchanged); $differing channels differ, max $maxDiff" +
                    (if (differing > 0) " within ${box.toList()}" else "")
            )
        }
        region.close(); tracker.close(); damaged.close(); full.close(); font.close(); scene.close(); gpu.close()
        if (outsideBlur > 0) fail("damage-clipped frames differ from full frames by up to $outsideBlur")
    }

    @Test
    fun aSizeChangeOrInvalidateDamagesWhole() {
        val context = RenderNodeContext(snapshotCache = true).apply { damageTracking = true }
        val scene = Scene(context)
        val root = scene.node(Rect(0f, 0f, 100f, 100f)) { drawRect(Rect(10f, 10f, 20f, 20f), Paint()) }
        val tracker = RenderNodeDamageTracker()
        fun frame(w: Int, h: Int): IntArray? {
            tracker.beginFrame(w, h); tracker.addNode(root); return tracker.endFrame()
        }
        assertNull(frame(100, 100))
        assertEquals(0, frame(100, 100)?.size)
        assertNull(frame(120, 100))
        tracker.invalidate()
        assertNull(frame(120, 100))
        root.record { drawRect(Rect(30f, 30f, 40f, 40f), Paint()) }
        val rects = assertNotNull(frame(120, 100))
        // The node's own content bounds, old and new, a pixel wider: not the whole target.
        assertTrue(area(rects) <= 2 * 12 * 12 + 2 * 31 * 31, "damage ${rects.toList()}")
        tracker.close(); scene.close()
    }
}

private fun RenderNode.record(draw: Canvas.() -> Unit) {
    val canvas = beginRecording()
    canvas.draw()
    endRecording()
}
