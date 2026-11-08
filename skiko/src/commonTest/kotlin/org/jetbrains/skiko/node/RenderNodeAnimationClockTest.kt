package org.jetbrains.skiko.node

import org.jetbrains.skia.*
import kotlin.test.Test
import kotlin.test.assertContentEquals
import kotlin.test.assertEquals
import kotlin.test.assertFalse
import kotlin.test.assertNotNull
import kotlin.test.assertNull
import kotlin.test.assertTrue

class RenderNodeAnimationClockTest {
    private val fastOutSlowIn = RenderNodeEasing.CubicBezier(0.4f, 0.0f, 0.2f, 1.0f)
    private val ms = 1_000_000L

    @Test
    fun linearTweenIsExact() {
        val curve = RenderNodeAnimationCurve.tween(100, 0, RenderNodeEasing.Linear)
        assertEquals(0f, curve.value(0, 0f, 1f))
        assertEquals(0.5f, curve.value(50 * ms, 0f, 1f))
        assertEquals(1f, curve.value(100 * ms, 0f, 1f))
        assertEquals(1f, curve.value(500 * ms, 0f, 1f))
        assertEquals(100 * ms, curve.durationNanos(0f, 1f))
        // The tween's velocity: the value now against 1 ms before, a second's worth.
        assertEquals((curve.value(50 * ms, 0f, 1f) - curve.value(49 * ms, 0f, 1f)) * 1000f, curve.velocity(50 * ms, 0f, 1f))
    }

    @Test
    fun tweenDelayAndCubicEasing() {
        val curve = RenderNodeAnimationCurve.tween(220, 60, RenderNodeEasing.CubicBezier(0.0f, 0.0f, 0.2f, 1.0f))
        assertEquals(280 * ms, curve.durationNanos(0f, 1f))
        assertEquals(0f, curve.value(30 * ms, 0f, 1f))
        val mid = curve.value(170 * ms, 0f, 1f)
        assertTrue(mid > 0.5f && mid < 1f, "$mid")
        // Monotonic, ends at the target.
        var last = 0f
        for (t in 60..280 step 5) {
            val v = curve.value(t * ms, 0f, 1f)
            assertTrue(v >= last, "at $t: $v < $last")
            last = v
        }
        assertEquals(1f, last)
    }

    @Test
    fun cubicEasingKnownValues() {
        // FastOutSlowInEasing is symmetric about its midpoint's slope only roughly; known points from Compose (1.13).
        val curve = RenderNodeAnimationCurve.tween(1000, 0, fastOutSlowIn)
        val half = curve.value(500 * ms, 0f, 1f)
        assertTrue(half > 0.75f && half < 0.85f, "$half")
    }

    @Test
    fun springSettlesAtTarget() {
        val curve = RenderNodeAnimationCurve.spring(dampingRatio = 0.72f, stiffness = 520f)
        val duration = curve.durationNanos(0f, 1f)
        assertTrue(duration in 200 * ms..2000 * ms, "$duration")
        assertEquals(1f, curve.value(duration, 0f, 1f))
        // Underdamped: overshoots.
        var max = 0f
        for (t in 0..duration / ms step 4) max = maxOf(max, curve.value(t * ms, 0f, 1f))
        assertTrue(max > 1f, "$max")
    }

    @Test
    fun keyframes() {
        val curve = RenderNodeAnimationCurve.keyframes(
            300, 0,
            listOf(RenderNodeKeyframe(100, 0.8f), RenderNodeKeyframe(200, 0.2f)),
        )
        assertEquals(0.8f, curve.value(100 * ms, 0f, 1f))
        assertEquals(0.4f, curve.value(50 * ms, 0f, 1f))
        assertEquals(0.5f, curve.value(150 * ms, 0f, 1f))
        assertEquals(0.6f, curve.value(250 * ms, 0f, 1f))
        assertEquals(1f, curve.value(300 * ms, 0f, 1f))
    }

    @Test
    fun clockAnimatesFromTheStartFrameAndEnds() {
        val context = RenderNodeContext()
        val node = RenderNode(context)
        val clock = RenderNodeAnimationClock()
        val curve = RenderNodeAnimationCurve.tween(100, 0, RenderNodeEasing.Linear)
        val id = clock.animate(node, RenderNodeProperty.TranslationX, curve, target = 100f, from = 0f)
        assertTrue(clock.isActive)
        assertTrue(clock.isRunning(id))
        assertEquals(0f, node.translationX)
        // Starts at the first tick.
        assertFalse(clock.tick(1_000 * ms))
        assertEquals(0f, node.translationX)
        assertTrue(clock.tick(1_050 * ms))
        assertEquals(50f, node.translationX)
        assertEquals(75f, clock.valueAt(node, RenderNodeProperty.TranslationX, 1_075 * ms))
        assertTrue(clock.tick(1_100 * ms))
        assertEquals(100f, node.translationX)
        assertFalse(clock.isActive)
        assertFalse(clock.isRunning(id))
        assertEquals(1L, clock.endedCount)
        clock.close()
        node.close()
        context.close()
    }

    @Test
    fun interruptionContinuesFromTheLastFrame() {
        val context = RenderNodeContext()
        val node = RenderNode(context)
        val clock = RenderNodeAnimationClock()
        val curve = RenderNodeAnimationCurve.tween(100, 0, RenderNodeEasing.Linear)
        clock.animate(node, RenderNodeProperty.ScaleX, curve, target = 2f, from = 1f, startTimeNanos = 0L)
        clock.tick(40 * ms)
        val at40 = curve.value(40 * ms, 1f, 2f)
        assertEquals(at40, node.scaleX)
        val velocity = clock.velocity(node, RenderNodeProperty.ScaleX)
        assertTrue(velocity > 9.9f && velocity < 10.1f, "$velocity")
        // Back to 1 from where it is, its time continuing from the frame at 40 ms.
        clock.animate(node, RenderNodeProperty.ScaleX, curve, target = 1f, startTimeNanos = RenderNodeAnimationClock.START_CONTINUE)
        assertEquals(at40, node.scaleX)
        clock.tick(90 * ms)
        assertEquals(curve.value(50 * ms, at40, 1f, velocity), node.scaleX)
        clock.close()
        node.close()
        context.close()
    }

    @Test
    fun alphaUsesALayerWhileTranslucent() {
        val context = RenderNodeContext()
        val node = RenderNode(context)
        val clock = RenderNodeAnimationClock()
        clock.snap(node, RenderNodeProperty.Alpha, 1f)
        assertNull(node.layerPaint)
        clock.animate(node, RenderNodeProperty.Alpha, RenderNodeAnimationCurve.tween(100, 0, RenderNodeEasing.Linear),
            target = 1f, from = 0f, startTimeNanos = 0L)
        assertNotNull(node.layerPaint)
        clock.tick(50 * ms)
        assertEquals(0.5f, node.alpha)
        assertEquals(0.5f, node.layerPaint!!.alphaf)
        clock.tick(100 * ms)
        assertEquals(1f, node.alpha)
        assertNull(node.layerPaint)
        clock.close()
        node.close()
        context.close()
    }

    @Test
    fun animatedNodeIsDrawnLiveAndDamaged() {
        val context = RenderNodeContext()
        val tracker = RenderNodeDamageTracker()
        val child = RenderNode(context)
        val parent = RenderNode(context)
        child.setNodeBounds(10f, 10f, 30f, 30f)
        child.beginRecording().drawRect(Rect.makeWH(20f, 20f), Paint().apply { color = Color.WHITE })
        child.endRecording()
        parent.setNodeBounds(0f, 0f, 64f, 64f)
        parent.beginRecording().also { child.drawInto(it) }
        parent.endRecording()
        val surface = Surface.makeRasterN32Premul(64, 64)

        fun frame(): IntArray? {
            tracker.beginFrame(64, 64)
            tracker.addNode(parent)
            val damage = tracker.endFrame()
            surface.canvas.clear(Color.BLACK)
            parent.drawInto(surface.canvas)
            return damage
        }
        fun red(x: Int, y: Int): Int {
            val bitmap = Bitmap().apply { allocPixels(ImageInfo.makeN32Premul(64, 64)) }
            surface.readPixels(bitmap, 0, 0)
            return Color.getR(bitmap.getColor(x, y))
        }

        assertNull(frame())
        assertContentEquals(IntArray(0), frame())
        val clock = RenderNodeAnimationClock()
        clock.animate(child, RenderNodeProperty.Alpha, RenderNodeAnimationCurve.tween(100, 0, RenderNodeEasing.Linear),
            target = 1f, from = 0f, startTimeNanos = 0L)
        val first = frame()!!
        assertEquals(red(20, 20), 0)
        assertTrue(first.isNotEmpty())
        clock.tick(50 * ms)
        val second = frame()!!
        // The child's bounds (a pixel wider for anti-aliasing), every frame its alpha changes.
        assertContentEquals(intArrayOf(9, 9, 31, 31), second)
        assertTrue(red(20, 20) in 126..129, "${red(20, 20)}")
        clock.tick(100 * ms)
        assertContentEquals(intArrayOf(9, 9, 31, 31), frame())
        assertEquals(255, red(20, 20))
        assertContentEquals(IntArray(0), frame())
        clock.close()
        surface.close()
        parent.close()
        child.close()
        tracker.close()
        context.close()
    }
}
