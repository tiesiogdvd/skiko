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
 * A page fading in on the clock, on a GPU canvas (Ganesh: the layer is kept from its first frame and composited at
 * each frame's alpha), draws the pixels of the same page faded by setting its alpha each frame (as a framework does),
 * and of a picture of it (saveLayer), frame by frame.
 */
@OptIn(ExperimentalForeignApi::class)
class RenderNodeAnimationClockGpuTest {
    private val width = 320
    private val height = 240
    private val ms = 1_000_000L

    private fun pixels(surface: Surface): ByteArray {
        val bitmap = Bitmap()
        bitmap.allocPixels(ImageInfo.makeN32Premul(width, height))
        assertTrue(surface.readPixels(bitmap, 0, 0))
        return bitmap.readPixels()!!
    }

    private class Tree(context: RenderNodeContext, font: Font, width: Int, height: Int) {
        val page = RenderNode(context)
        val root = RenderNode(context)
        val cards = (0 until 12).map { RenderNode(context) }

        init {
            cards.forEachIndexed { i, card ->
                card.bounds = Rect(10f + (i % 4) * 70f, 10f + (i / 4) * 60f, 70f + (i % 4) * 70f, 60f + (i / 4) * 60f)
                val c = card.beginRecording()
                c.drawRRect(RRect.makeXYWH(0f, 0f, 60f, 50f, 8f), Paint().apply { color = Color.makeRGB(40 + i * 17, 90, 160) })
                c.drawString("Card $i", 5.5f, 30.25f, font, Paint().apply { color = Color.WHITE })
                card.endRecording()
            }
            page.bounds = Rect(20.5f, 16f, 300.5f, 216f)
            val p = page.beginRecording()
            p.drawRRect(RRect.makeXYWH(0f, 0f, 280f, 200f, 14f), Paint().apply { color = Color.makeRGB(32, 36, 44) })
            p.drawString("A page fading in", 12.25f, 40.5f, font, Paint().apply { color = Color.WHITE })
            p.drawCircle(220f, 120f, 40f, Paint().apply { color = Color.makeRGB(230, 120, 40) })
            page.endRecording()
            root.bounds = Rect(0f, 0f, width.toFloat(), height.toFloat())
            val r = root.beginRecording()
            r.drawRect(Rect(0f, 0f, width.toFloat(), height.toFloat()), Paint().apply { color = Color.makeRGB(15, 17, 28) })
            cards.forEach { it.drawInto(r) }
            page.drawInto(r)
            root.endRecording()
        }

        fun close() {
            root.close(); page.close(); cards.forEach { it.close() }
        }
    }

    @Test
    fun clockFadeDrawsTheFramesOfASetterFade() {
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val gpu = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr())
        val font = Font(null, 13f)
        val animated = Tree(RenderNodeContext(snapshotCache = true), font, width, height)
        val set = Tree(RenderNodeContext(snapshotCache = true), font, width, height)
        fun makeSurface() = Surface.makeRenderTarget(gpu, false, ImageInfo.makeN32Premul(width, height), 0, SurfaceOrigin.TOP_LEFT, null, false)
        val a = makeSurface()
        val b = makeSurface()
        val c = makeSurface()

        val clock = RenderNodeAnimationClock()
        // FlowMotion.pageIn: 220 ms after 60, LinearOutSlowInEasing.
        val curve = RenderNodeAnimationCurve.tween(220, 60, RenderNodeEasing.CubicBezier(0.0f, 0.0f, 0.2f, 1.0f))
        val start = 1_000_000 * ms
        clock.animate(animated.page, RenderNodeProperty.Alpha, curve, target = 1f, from = 0f, startTimeNanos = start)
        var worstSet = 0
        var worstPicture = 0
        var frames = 0
        var time = start
        while (true) {
            clock.tick(time)
            // The framework's way: the same curve's value at Compose's play time (the elapsed nanos over a Float
            // duration scale), set as Compose's graphicsLayer sets it.
            val alpha = curve.value(((time - start) / 1f).toLong(), 0f, 1f)
            set.page.alpha = alpha
            set.page.layerPaint = if (alpha < 1f) Paint().apply { setAlphaf(alpha) } else null
            assertEquals(alpha, animated.page.alpha)

            a.canvas.clear(Color.WHITE); animated.root.drawInto(a.canvas)
            b.canvas.clear(Color.WHITE); set.root.drawInto(b.canvas)
            val recorder = PictureRecorder()
            set.root.drawInto(recorder.beginRecording(Rect(0f, 0f, width.toFloat(), height.toFloat())))
            val picture = recorder.finishRecordingAsPicture()
            c.canvas.clear(Color.WHITE); c.canvas.drawPicture(picture)
            gpu.flush(); gpu.submit(syncCpu = true)
            picture.close()
            val pa = pixels(a); val pb = pixels(b); val pc = pixels(c)
            var maxSet = 0; var maxPicture = 0; var differing = 0
            for (i in pa.indices) {
                val ds = kotlin.math.abs((pa[i].toInt() and 0xFF) - (pb[i].toInt() and 0xFF))
                val dp = kotlin.math.abs((pa[i].toInt() and 0xFF) - (pc[i].toInt() and 0xFF))
                if (ds != 0) differing++
                maxSet = maxOf(maxSet, ds); maxPicture = maxOf(maxPicture, dp)
            }
            println("frame $frames (alpha $alpha): clock vs setters $differing channels differ, max $maxSet; vs saveLayer max $maxPicture")
            worstSet = maxOf(worstSet, maxSet); worstPicture = maxOf(worstPicture, maxPicture)
            frames++
            if (!clock.isActive) break
            time += 8_333_333L
        }
        assertTrue(frames in 30..40, "$frames frames")
        clock.close(); a.close(); b.close(); c.close(); animated.close(); set.close(); font.close(); gpu.close()
        if (worstSet > 1) fail("the clock's fade differs from the setters' by up to $worstSet")
        if (worstPicture > 2) fail("the clock's fade differs from saveLayer by up to $worstPicture")
    }
}
