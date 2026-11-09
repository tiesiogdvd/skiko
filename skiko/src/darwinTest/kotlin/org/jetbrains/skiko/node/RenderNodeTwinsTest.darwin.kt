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
 * A frame drawn through render twins has to equal the same frame drawn from its nodes. Two identical trees change the
 * same way frame after frame: one is drawn as it is, the other only through its twins (recorded, synced, the twin
 * frame drawn), on Metal and on a raster canvas. The changes cover the layer atlas (alpha layers), a retained blur, a
 * shadow, clips, an alpha without a layer, a node drawn twice, a child added and removed (and closed), a re-recorded
 * leaf inside a layer and the root recorded anew; frames without changes visit no node.
 */
@OptIn(ExperimentalForeignApi::class)
class RenderNodeTwinsTest {
    private val width = 320
    private val height = 240

    /** One tree; [changes] are applied to both trees in turn. */
    private class Tree(val context: RenderNodeContext) {
        val nodes = mutableListOf<RenderNode>()
        val font = Font(null, 11f)

        fun node(bounds: Rect, block: RenderNode.() -> Unit = {}, draw: Canvas.() -> Unit): RenderNode {
            val node = RenderNode(context)
            node.bounds = bounds
            node.block()
            node.recordContent(draw)
            nodes += node
            return node
        }

        val cards = (0 until 12).map { index ->
            node(Rect(10f + (index % 6) * 50f, 10f + (index / 6) * 50f, 50f + (index % 6) * 50f, 50f + (index / 6) * 50f), {
                layerPaint = Paint().apply { setAlphaf(0.6f + index * 0.03f) }
            }) {
                drawRRect(RRect.makeXYWH(0f, 0f, 40f, 40f, 6f), Paint().apply { color = Color.makeRGB(40 + index * 15, 90, 200 - index * 10) })
                drawRect(Rect(6f, 6f, 20f, 14f), Paint().apply { color = Color.WHITE })
            }
        }
        var blurColor = Color.makeRGB(230, 120, 40)
        val blurred = node(Rect(0f, 0f, 110f, 80f), {
            layerPaint = Paint().apply { imageFilter = ImageFilter.makeBlur(6f, 6f, FilterTileMode.DECAL) }
        }) { drawCircle(55f, 40f, 30f, Paint().apply { color = blurColor }) }
        val panel = node(Rect(190f, 120f, 300f, 200f), {
            clip = true
            setClipRRect(0f, 0f, 110f, 80f, floatArrayOf(16f), antiAlias = true)
        }) { blurred.drawInto(this) }
        val fader = node(Rect(20f, 130f, 80f, 170f), { alpha = 0.5f }) {
            drawRect(Rect(0f, 0f, 60f, 40f), Paint().apply { color = Color.MAGENTA })
            drawRect(Rect(10f, 10f, 50f, 30f), Paint().apply { color = Color.YELLOW })
        }
        val raised = node(Rect(100f, 170f, 160f, 220f), {
            clip = true
            setClipRRect(0f, 0f, 60f, 50f, floatArrayOf(8f), antiAlias = true)
            shadowElevation = 4f
        }) { drawRect(Rect(0f, 0f, 60f, 50f), Paint().apply { color = Color.WHITE }) }
        var extra: RenderNode? = null
        val twice = node(Rect(0f, 0f, 20f, 12f)) { drawRect(Rect(0f, 0f, 20f, 12f), Paint().apply { color = Color.BLUE }) }
        val holder = node(Rect(10f, 180f, 70f, 235f)) {}
        fun recordHolder() = holder.recordContent {
            drawRect(Rect(0f, 0f, 60f, 55f), Paint().apply { color = Color.makeRGB(40, 40, 40) })
            extra?.let { save(); translate(15f, 5f); it.drawInto(this); restore() }
            twice.drawInto(this)
            save(); translate(30f, 35f); twice.drawInto(this); restore()
        }
        var labelColor = Color.WHITE
        val grid = node(Rect(0f, 0f, width(), 110f)) {}
        fun recordGrid() = grid.recordContent { cards.forEach { it.drawInto(this) } }
        val root = node(Rect(0f, 0f, width(), height())) {}
        fun recordRoot() = root.recordContent {
            drawRect(Rect(0f, 0f, width(), height()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            grid.drawInto(this)
            panel.drawInto(this)
            drawRRect(RRect.makeXYWH(180.5f, 214.25f, 24f, 11f, 3f), Paint().apply { color = labelColor; isAntiAlias = true })
            fader.drawInto(this)
            raised.drawInto(this)
            holder.drawInto(this)
        }

        init {
            recordHolder()
            recordGrid()
            recordRoot()
        }

        fun close() {
            nodes.forEach { it.close() }
            extra?.close()
            font.close()
            context.close()
        }

        companion object {
            fun width() = 320f
            fun height() = 240f
        }
    }

    private val changes = listOf<Tree.() -> Unit>(
        {},
        {},
        { cards[3].translationX = 7.5f },
        { cards[5].layerPaint = Paint().apply { setAlphaf(0.9f) } },
        { cards[7].recordContent { drawRect(Rect(0f, 0f, 40f, 40f), Paint().apply { color = Color.GREEN }) } },
        { fader.alpha = 0.8f },
        { blurColor = Color.makeRGB(40, 200, 90); blurred.recordContent { drawCircle(55f, 40f, 30f, Paint().apply { color = blurColor }) } },
        {},
        {},
        {
            extra = RenderNode(context).also {
                it.bounds = Rect(0f, 0f, 30f, 20f)
                it.recordContent { drawRect(Rect(0f, 0f, 30f, 20f), Paint().apply { color = Color.GREEN }) }
            }
            recordHolder()
        },
        { extra?.alpha = 0.4f },
        { val gone = extra; extra = null; recordHolder(); gone?.close() },
        { raised.shadowElevation = 10f },
        { twice.recordContent { drawRect(Rect(0f, 0f, 20f, 12f), Paint().apply { color = Color.WHITE }) } },
        { panel.translationY = -12.25f; raised.rotationZ = 8f },
        { labelColor = Color.makeRGB(255, 200, 120); recordRoot() },
        { cards.forEachIndexed { i, card -> card.scaleX = 1f + i * 0.01f } },
        { context.setLightingInfo(centerX = 200f, centerY = -50f, centerZ = 500f, radius = 600f, ambientShadowAlpha = 0.05f, spotShadowAlpha = 0.25f) },
        {},
    )

    private fun pixels(surface: Surface): ByteArray {
        val bitmap = Bitmap()
        bitmap.allocPixels(ImageInfo.makeN32Premul(width, height))
        assertTrue(surface.readPixels(bitmap, 0, 0))
        return bitmap.readPixels()!!.also { bitmap.close() }
    }

    private fun maxDifference(a: ByteArray, b: ByteArray): Int {
        var max = 0
        for (i in a.indices) max = maxOf(max, kotlin.math.abs((a[i].toInt() and 0xFF) - (b[i].toInt() and 0xFF)))
        return max
    }

    private fun lighting(context: RenderNodeContext) = context.apply {
        setLightingInfo(centerX = 160f, centerY = -100f, centerZ = 600f, radius = 800f, ambientShadowAlpha = 0.04f, spotShadowAlpha = 0.19f)
    }

    private fun check(makeSurface: () -> Surface, flush: () -> Unit, tolerance: Int, label: String) {
        val live = Tree(lighting(RenderNodeContext(snapshotCache = true)))
        val twinned = Tree(lighting(RenderNodeContext(snapshotCache = true)))
        val twins = RenderNodeTwins()
        val a = makeSurface()
        val b = makeSurface()
        var worst = 0
        for ((index, change) in changes.withIndex()) {
            live.change()
            twinned.change()
            a.canvas.clear(Color.WHITE)
            live.root.drawInto(a.canvas)

            val recording = twins.beginRecording(width.toFloat(), height.toFloat())
            recording.clear(Color.WHITE)
            twinned.root.drawInto(recording)
            val frame = twins.finishRecording()
            val twinFrame = twins.sync(frame)
            frame.close()
            twins.releasePending()
            b.canvas.clear(Color.BLACK)
            twinFrame.draw(b.canvas)
            twinFrame.close()
            flush()

            val difference = maxDifference(pixels(a), pixels(b))
            println("$label frame $index: twins visited ${twins.visitedNodes}, recorded ${twins.recordedNodes}; max difference $difference")
            if (index > 0 && (index == 1 || index == 7 || index == 8 || index == changes.lastIndex)) {
                assertEquals(0, twins.visitedNodes, "$label frame $index: nothing changed, so no node is visited")
            }
            worst = maxOf(worst, difference)
        }
        twins.close()
        a.close(); b.close()
        live.close(); twinned.close()
        if (worst > tolerance) fail("$label: twin frames differ from live frames by up to $worst")
    }

    @Test
    fun twinFramesMatchLiveFramesOnMetal() {
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val gpu = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr())
        // Retained layers are made after a few equal draws, so the two trees retain theirs on the same frames; a
        // retained blur may still differ from its saveLayer by 1.
        check(
            makeSurface = { Surface.makeRenderTarget(gpu, false, ImageInfo.makeN32Premul(width, height), 0, SurfaceOrigin.TOP_LEFT, null, false)!! },
            flush = { gpu.flush(); gpu.submit(syncCpu = true) },
            tolerance = 1,
            label = "metal",
        )
        gpu.close()
    }

    @Test
    fun damageClippedTwinFramesMatchFullFrames() {
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val gpu = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr())
        fun surface() = Surface.makeRenderTarget(gpu, false, ImageInfo.makeN32Premul(width, height), 0, SurfaceOrigin.TOP_LEFT, null, false)!!
        // The live tree drawn whole; the twinned one walked for damage (the twins take the walk's records), its twins
        // drawn only inside the damage over the last frame, as a render thread draws a back buffer.
        val live = Tree(lighting(RenderNodeContext(snapshotCache = true)))
        val twinned = Tree(lighting(RenderNodeContext(snapshotCache = true).apply { damageTracking = true }))
        val twins = RenderNodeTwins()
        val tracker = RenderNodeDamageTracker()
        val full = surface()
        val damaged = surface()
        // The same damage drawn from a third tree's own nodes: what damage clipping alone gives, without twins.
        val reference = Tree(lighting(RenderNodeContext(snapshotCache = true).apply { damageTracking = true }))
        val referenceTracker = RenderNodeDamageTracker()
        val referenceDamaged = surface()
        var worstReference = 0
        var worst = 0
        for ((index, change) in changes.withIndex()) {
            // A host's frame: the caches count each tree's draws in it once.
            RenderNodeCaches.beginFrame()
            live.change()
            twinned.change()
            reference.change()
            full.canvas.clear(Color.WHITE)
            live.root.drawInto(full.canvas)

            referenceTracker.beginFrame(width, height)
            referenceTracker.addNode(reference.root)
            val referenceRects = referenceTracker.endFrame()
            fun drawReference(canvas: Canvas) {
                if (referenceRects == null) {
                    canvas.clear(Color.WHITE); reference.root.drawInto(canvas); return
                }
                for (i in referenceRects.indices step 4) {
                    canvas.save()
                    canvas.clipRect(Rect(referenceRects[i].toFloat(), referenceRects[i + 1].toFloat(), referenceRects[i + 2].toFloat(), referenceRects[i + 3].toFloat()))
                    canvas.clear(Color.WHITE)
                    reference.root.drawInto(canvas)
                    canvas.restore()
                }
            }
            drawReference(referenceDamaged.canvas)

            tracker.beginFrame(width, height)
            tracker.addNode(twinned.root)
            val rects = tracker.endFrame()
            val recording = twins.beginRecording(width.toFloat(), height.toFloat())
            if (rects == null) {
                recording.clear(Color.WHITE)
                twinned.root.drawInto(recording)
            } else {
                for (i in rects.indices step 4) {
                    recording.save()
                    recording.clipRect(Rect(rects[i].toFloat(), rects[i + 1].toFloat(), rects[i + 2].toFloat(), rects[i + 3].toFloat()))
                    recording.clear(Color.WHITE)
                    twinned.root.drawInto(recording)
                    recording.restore()
                }
            }
            val frame = twins.finishRecording()
            val twinFrame = twins.sync(frame)
            frame.close()
            twinFrame.draw(damaged.canvas)
            twinFrame.close()
            gpu.flush(); gpu.submit(syncCpu = true)
            val difference = maxDifference(pixels(full), pixels(damaged))
            val referenceDifference = maxDifference(pixels(full), pixels(referenceDamaged))
            val twinAgainstReference = maxDifference(pixels(referenceDamaged), pixels(damaged))
            println(
                "damage frame $index: ${rects?.let { "${it.size / 4} rects" } ?: "whole"}, twins visited ${twins.visitedNodes}; " +
                    "max difference $difference (the nodes' own damage-clipped frames: $referenceDifference; twins against them: $twinAgainstReference)"
            )
            worst = maxOf(worst, difference)
            worstReference = maxOf(worstReference, twinAgainstReference)
        }
        tracker.close(); referenceTracker.close(); twins.close()
        full.close(); damaged.close(); referenceDamaged.close()
        live.close(); twinned.close(); reference.close(); gpu.close()
        // Twins draw the damage as the nodes themselves do.
        if (worstReference > 0) fail("damage-clipped twin frames differ from the nodes' own by up to $worstReference")
        // A retained blur may differ from its saveLayer by 1, and a damage-clipped buffer keeps whichever drew last.
        if (worst > 2) fail("damage-clipped twin frames differ from full frames by up to $worst")
    }

    /**
     * The textures nodes keep (retained layers, the raster cache) belong to the twins where twins are drawn: a twin
     * frame drawn with them, frame after frame while a badge inside a cached grid changes, equals the same twin frame
     * drawn without them (within 1/255 on Ganesh, the raster cache's rounding), and the twins hold textures.
     */
    @Test
    fun twinsKeepTexturesThatMatchDrawingWithoutThem() {
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val gpu = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr())
        val context = RenderNodeContext(snapshotCache = true)
        val nodes = mutableListOf<RenderNode>()
        fun node(bounds: Rect, block: RenderNode.() -> Unit = {}, draw: Canvas.() -> Unit) = RenderNode(context).also {
            it.bounds = bounds
            it.block()
            it.recordContent(draw)
            nodes += it
        }
        val font = Font(null, 11f)
        val cards = (0 until 8).map { i ->
            node(Rect(6f + (i % 4) * 76f, 8f + (i / 4) * 50f, 76f + (i % 4) * 76f, 52f + (i / 4) * 50f), {
                clip = true
                setClipRRect(0f, 0f, 70f, 44f, floatArrayOf(8f), antiAlias = true)
            }) {
                drawRect(Rect(0f, 0f, 70f, 44f), Paint().apply { color = Color.makeRGB(40 + i * 23, 80, 150) })
                drawCircle(12f, 12f, 7f, Paint().apply { color = Color.makeARGB(200, 255, 255, 255) })
                drawString("Card $i", 4.5f, 36.3f, font, Paint().apply { color = Color.WHITE })
                drawString("${i * 7}", 50.5f, 22.3f, font, Paint().apply { color = Color.makeARGB(180, 255, 230, 200) })
            }
        }
        var badgeColor = Color.RED
        val badge = node(Rect(290f, 4f, 310f, 16f)) { drawRect(Rect(0f, 0f, 20f, 12f), Paint().apply { color = badgeColor }) }
        val grid = node(Rect(2f, 2f, 318f, 112f), {
            clip = true
            setClipRRect(0f, 0f, 316f, 110f, floatArrayOf(12f), antiAlias = true)
        }) {
            drawRect(Rect(0f, 0f, 316f, 110f), Paint().apply { color = Color.makeRGB(24, 26, 33) })
            cards.forEach { it.drawInto(this) }
            badge.drawInto(this)
        }
        val page = node(Rect(10.25f, 120f, 170.25f, 230f), { layerPaint = Paint().apply { setAlphaf(0.7f) } }) {
            drawRRect(RRect.makeXYWH(0f, 0f, 160f, 110f, 10f), Paint().apply { color = Color.makeRGB(70, 80, 100) })
            drawString("Static page", 8.25f, 40.5f, font, Paint().apply { color = Color.WHITE })
        }
        val root = node(Rect(0f, 0f, width.toFloat(), height.toFloat())) {
            drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            grid.drawInto(this)
            page.drawInto(this)
        }
        val twins = RenderNodeTwins()
        fun surface() = Surface.makeRenderTarget(gpu, false, ImageInfo.makeN32Premul(width, height), 0, SurfaceOrigin.TOP_LEFT, null, false)!!
        val cached = surface()
        val reference = surface()
        var worst = 0
        var held = 0
        for (frame in 0 until 10) {
            if (frame == 5) {
                badgeColor = Color.GREEN
                badge.recordContent { drawRect(Rect(0f, 0f, 20f, 12f), Paint().apply { color = badgeColor }) }
            }
            // The drawing thread's frame (a host drawing twins on a render thread calls it there).
            RenderNodeCaches.beginFrame()
            val recording = twins.beginRecording(width.toFloat(), height.toFloat())
            root.drawInto(recording)
            val recorded = twins.finishRecording()
            val twinFrame = twins.sync(recorded)
            recorded.close()
            twins.releasePending()
            cached.canvas.clear(Color.WHITE)
            twinFrame.draw(cached.canvas)
            RenderNodeCaches.drawWithoutCaches {
                reference.canvas.clear(Color.WHITE)
                twinFrame.draw(reference.canvas)
            }
            twinFrame.close()
            gpu.flush(); gpu.submit(syncCpu = true)
            val difference = maxDifference(pixels(cached), pixels(reference))
            held = maxOf(held, RenderNodeCaches.count)
            println("twin textures frame $frame: cached vs without: max difference $difference; ${RenderNodeCaches.count} textures")
            worst = maxOf(worst, difference)
        }
        twins.close()
        cached.close(); reference.close()
        nodes.forEach { it.close() }
        context.close(); font.close()
        RenderNodeCaches.purge()
        gpu.close()
        assertTrue(held >= 2, "the twins of the grid and the page keep textures")
        if (worst > 1) fail("twin textures differ from drawing without them by up to $worst")
    }

    @Test
    fun twinFramesMatchLiveFramesOnRaster() {
        check(
            makeSurface = { Surface.makeRaster(ImageInfo.makeN32Premul(width, height)) },
            flush = {},
            tolerance = 0,
            label = "raster",
        )
    }

    @Test
    fun aClosedNodesTwinIsReleasedOnTheDrawingThread() {
        val context = RenderNodeContext(snapshotCache = true)
        val twins = RenderNodeTwins()
        val child = RenderNode(context).apply { bounds = Rect(0f, 0f, 10f, 10f); recordContent { drawRect(Rect(0f, 0f, 10f, 10f), Paint()) } }
        val root = RenderNode(context).apply { bounds = Rect(0f, 0f, 20f, 20f); recordContent { child.drawInto(this) } }
        fun frame(): RenderNodeFrame {
            val canvas = twins.beginRecording(20f, 20f)
            root.drawInto(canvas)
            val live = twins.finishRecording()
            return twins.sync(live).also { live.close() }
        }
        val first = frame()
        assertEquals(2, twins.visitedNodes)
        // The child goes: the root drops it, the first frame still draws its twin.
        root.recordContent { drawRect(Rect(0f, 0f, 5f, 5f), Paint()) }
        child.close()
        val surface = Surface.makeRaster(ImageInfo.makeN32Premul(20, 20))
        first.draw(surface.canvas)
        first.close()
        val second = frame()
        assertEquals(1, twins.visitedNodes)
        twins.releasePending()
        second.draw(surface.canvas)
        second.close()
        twins.close()
        root.close(); surface.close(); context.close()
    }
}

private fun RenderNode.recordContent(draw: Canvas.() -> Unit) {
    val canvas = beginRecording()
    canvas.draw()
    endRecording()
}
