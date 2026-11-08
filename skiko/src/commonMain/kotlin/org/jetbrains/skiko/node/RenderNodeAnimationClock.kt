package org.jetbrains.skiko.node

import org.jetbrains.skia.*
import org.jetbrains.skia.impl.*
import org.jetbrains.skia.impl.Library.Companion.staticLoad

/** A [RenderNode] property a [RenderNodeAnimationClock] animates: one that changes how the recorded content is drawn. */
enum class RenderNodeProperty {
    Alpha,
    TranslationX,
    TranslationY,
    ScaleX,
    ScaleY,
    RotationX,
    RotationY,
    RotationZ,
}

/**
 * How an animated alpha is drawn, as Compose's `CompositingStrategy`: [Auto] composites the content through a layer
 * while the alpha is below 1 (a `graphicsLayer`'s alpha), [Offscreen] always through a layer, [ModulateAlpha] never.
 * Under [Auto] and [Offscreen] the clock owns the node's [RenderNode.layerPaint].
 */
enum class RenderNodeAlphaCompositing { Auto, Offscreen, ModulateAlpha }

/** An easing a [RenderNodeAnimationCurve] can be evaluated with natively: Compose's `LinearEasing` or a `CubicBezierEasing`. */
sealed class RenderNodeEasing {
    object Linear : RenderNodeEasing()

    data class CubicBezier(val a: Float, val b: Float, val c: Float, val d: Float) : RenderNodeEasing()

    internal fun encode(into: FloatArray, at: Int) {
        when (this) {
            Linear -> into[at] = 0f
            is CubicBezier -> {
                into[at] = 1f
                into[at + 1] = a
                into[at + 2] = b
                into[at + 3] = c
                into[at + 4] = d
            }
        }
    }
}

/** A keyframe of [RenderNodeAnimationCurve.keyframes]: [value] at [timeMillis], [easing] applying until the next one. */
data class RenderNodeKeyframe(val timeMillis: Int, val value: Float, val easing: RenderNodeEasing = RenderNodeEasing.Linear)

/**
 * One of Compose's finite animation specs for a Float, evaluated natively the way Compose evaluates it (the same
 * operations in the same precision, so the same values): `tween`, `spring`, `snap` and `keyframes` (linear
 * interpolation between keyframes). [value], [velocity] and [durationNanos] are those of a `TargetBasedAnimation` of
 * it: past the duration the value is the target.
 */
class RenderNodeAnimationCurve private constructor(internal val data: FloatArray) {
    companion object {
        init {
            staticLoad()
        }

        fun tween(durationMillis: Int, delayMillis: Int = 0, easing: RenderNodeEasing): RenderNodeAnimationCurve {
            val data = FloatArray(8)
            data[0] = 0f
            data[1] = durationMillis.toFloat()
            data[2] = delayMillis.toFloat()
            easing.encode(data, 3)
            return RenderNodeAnimationCurve(data)
        }

        fun spring(dampingRatio: Float, stiffness: Float, visibilityThreshold: Float = 0.01f) =
            RenderNodeAnimationCurve(floatArrayOf(1f, dampingRatio, stiffness, visibilityThreshold))

        fun keyframes(durationMillis: Int, delayMillis: Int = 0, keyframes: List<RenderNodeKeyframe>): RenderNodeAnimationCurve {
            val data = FloatArray(4 + keyframes.size * 7)
            data[0] = 2f
            data[1] = durationMillis.toFloat()
            data[2] = delayMillis.toFloat()
            data[3] = keyframes.size.toFloat()
            keyframes.forEachIndexed { index, keyframe ->
                val at = 4 + index * 7
                data[at] = keyframe.timeMillis.toFloat()
                data[at + 1] = keyframe.value
                keyframe.easing.encode(data, at + 2)
            }
            return RenderNodeAnimationCurve(data)
        }

        fun snap(delayMillis: Int = 0) = RenderNodeAnimationCurve(floatArrayOf(3f, delayMillis.toFloat()))
    }

    fun value(playTimeNanos: Long, initial: Float, target: Float, initialVelocity: Float = 0f): Float = interopScope {
        Stats.onNativeCall()
        RenderNodeAnimationCurve_nValue(toInterop(data), data.size, playTimeNanos, initial, target, initialVelocity, false)
    }

    fun velocity(playTimeNanos: Long, initial: Float, target: Float, initialVelocity: Float = 0f): Float = interopScope {
        Stats.onNativeCall()
        RenderNodeAnimationCurve_nValue(toInterop(data), data.size, playTimeNanos, initial, target, initialVelocity, true)
    }

    fun durationNanos(initial: Float, target: Float, initialVelocity: Float = 0f): Long = interopScope {
        Stats.onNativeCall()
        RenderNodeAnimationCurve_nDurationNanos(toInterop(data), data.size, initial, target, initialVelocity)
    }
}

/**
 * Animates [RenderNode] properties on a frame clock, natively, without the framework that recorded the nodes: a
 * property's value is a [RenderNodeAnimationCurve] of the frame time, applied before a frame is drawn ([tick]). The
 * node then counts as changed for a [RenderNodeDamageTracker] and is drawn from the recordings it has, so an animated
 * frame records nothing and runs no framework code; while a node animates only its alpha, a layer it has keeps its
 * texture and is composited again (Ganesh).
 *
 * The host ticks the clock once a frame at the frame's time, before drawing, and keeps asking for frames while it is
 * [active][isActive]. An animation starts at the frame time given (the frame a framework would have started it on, so
 * that the frames are the ones it would have drawn) or at the next [tick]; from a value or where the property is; with
 * a velocity or the velocity of the animation it replaces. Replacing a running animation with
 * [START_CONTINUE] continues from its last frame (value, velocity and time), as Compose's `Animatable.animateTo` does
 * when it interrupts itself. The value at the start is applied at once, the target at the end, and then the animation
 * ends ([isRunning], [endedCount]). While an animation runs, the clock owns the property: a value set on the node is
 * overwritten by the next tick.
 *
 * Owned by one thread, like the nodes; it holds the nodes it animates while they animate.
 */
class RenderNodeAnimationClock internal constructor(ptr: NativePointer, managed: Boolean = true) : RefCnt(ptr, managed) {
    companion object {
        init {
            staticLoad()
        }

        /** A start time: at the first [tick] after the start. */
        const val START_ON_NEXT_FRAME: Long = Long.MIN_VALUE

        /** A start time: the last frame of the animation replaced, or the next tick without one. */
        const val START_CONTINUE: Long = Long.MIN_VALUE + 1
    }

    constructor() : this(RenderNodeAnimationClock_nMake()) {
        Stats.onNativeCall()
    }

    /**
     * Starts animating [property] of [node] to [target] along [curve], replacing an animation of it. [from] and
     * [velocity] NaN take the property's current value and the replaced animation's velocity (0 without one). The play
     * time is the frame time since [startTimeNanos] divided by [durationScale] (Compose's `MotionDurationScale`; 0 jumps
     * to the end). Returns the animation's id.
     */
    fun animate(
        node: RenderNode,
        property: RenderNodeProperty,
        curve: RenderNodeAnimationCurve,
        target: Float,
        from: Float = Float.NaN,
        velocity: Float = Float.NaN,
        startTimeNanos: Long = START_ON_NEXT_FRAME,
        durationScale: Float = 1f,
        alphaCompositing: RenderNodeAlphaCompositing = RenderNodeAlphaCompositing.Auto,
    ): Long = try {
        Stats.onNativeCall()
        interopScope {
            RenderNodeAnimationClock_nStart(
                _ptr, getPtr(node), property.ordinal, toInterop(curve.data), curve.data.size,
                from, target, velocity, startTimeNanos, durationScale, alphaCompositing.ordinal,
            )
        }
    } finally {
        reachabilityBarrier(node)
        reachabilityBarrier(this)
    }

    /** Ends the animation of [property] of [node] where it is. */
    fun cancel(node: RenderNode, property: RenderNodeProperty) = try {
        Stats.onNativeCall()
        RenderNodeAnimationClock_nCancel(_ptr, getPtr(node), property.ordinal)
    } finally {
        reachabilityBarrier(node)
        reachabilityBarrier(this)
    }

    /** Ends every animation of [node] where it is. */
    fun cancelAll(node: RenderNode) = try {
        Stats.onNativeCall()
        RenderNodeAnimationClock_nCancel(_ptr, getPtr(node), -1)
    } finally {
        reachabilityBarrier(node)
        reachabilityBarrier(this)
    }

    /** Sets [property] of [node] to [value] now, as the clock applies values, ending an animation of it. */
    fun snap(
        node: RenderNode,
        property: RenderNodeProperty,
        value: Float,
        alphaCompositing: RenderNodeAlphaCompositing = RenderNodeAlphaCompositing.Auto,
    ) = try {
        Stats.onNativeCall()
        RenderNodeAnimationClock_nSnap(_ptr, getPtr(node), property.ordinal, value, alphaCompositing.ordinal)
    } finally {
        reachabilityBarrier(node)
        reachabilityBarrier(this)
    }

    /** Applies every animation's value at [frameTimeNanos]; whether any property changed. */
    fun tick(frameTimeNanos: Long): Boolean = try {
        Stats.onNativeCall()
        RenderNodeAnimationClock_nTick(_ptr, frameTimeNanos)
    } finally {
        reachabilityBarrier(this)
    }

    /** Whether any animation runs: the host keeps ticking frames while it does. */
    val isActive: Boolean
        get() = try {
            Stats.onNativeCall()
            RenderNodeAnimationClock_nQuery(_ptr, 0, 0L) != 0L
        } finally {
            reachabilityBarrier(this)
        }

    /** How many animations ended (finished, replaced or cancelled) since the clock was made. */
    val endedCount: Long
        get() = try {
            Stats.onNativeCall()
            RenderNodeAnimationClock_nQuery(_ptr, 1, 0L)
        } finally {
            reachabilityBarrier(this)
        }

    /** Whether the animation [id] ([animate]) still runs. */
    fun isRunning(id: Long): Boolean = try {
        Stats.onNativeCall()
        RenderNodeAnimationClock_nQuery(_ptr, 2, id) != 0L
    } finally {
        reachabilityBarrier(this)
    }

    /** [property]'s value as last applied. */
    fun value(node: RenderNode, property: RenderNodeProperty): Float = nodeQuery(node, property, 0, 0L)

    /** The velocity of [property]'s animation at its last frame (0 when it does not animate). */
    fun velocity(node: RenderNode, property: RenderNodeProperty): Float = nodeQuery(node, property, 1, 0L)

    /** The value [property]'s animation has at [frameTimeNanos] (the last applied when it does not animate). */
    fun valueAt(node: RenderNode, property: RenderNodeProperty, frameTimeNanos: Long): Float =
        nodeQuery(node, property, 2, frameTimeNanos)

    private fun nodeQuery(node: RenderNode, property: RenderNodeProperty, what: Int, time: Long): Float = try {
        Stats.onNativeCall()
        RenderNodeAnimationClock_nNodeQuery(_ptr, getPtr(node), property.ordinal, what, time)
    } finally {
        reachabilityBarrier(node)
        reachabilityBarrier(this)
    }
}

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationCurve_1nValue")
// No ForceNativeThreadState: evaluates a curve, a few hundred instructions at most.
// Native call path: AnimationCurve::value, AnimationCurve::velocity
private external fun RenderNodeAnimationCurve_nValue(
    data: InteropPointer, size: Int, playTimeNanos: Long, initial: Float, target: Float, velocity: Float, wantVelocity: Boolean,
): Float

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationCurve_1nDurationNanos")
// No ForceNativeThreadState: evaluates a curve's duration (a spring's: a few Newton iterations).
// Native call path: AnimationCurve::durationNanos
private external fun RenderNodeAnimationCurve_nDurationNanos(
    data: InteropPointer, size: Int, initial: Float, target: Float, velocity: Float,
): Long

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nMake")
// No ForceNativeThreadState: allocates an empty clock.
// Native call path: sk_make_sp<RenderNodeAnimationClock>
private external fun RenderNodeAnimationClock_nMake(): NativePointer

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nStart")
// Needs ForceNativeThreadState: applying the first value sets a property, which can clear the cached pictures of the
// nodes drawing this one (releasing fonts and images).
// Native call path: RenderNodeAnimationClock::start -> RenderNode::setAlpha -> invalidateSnapshot
@ForceNativeThreadState
private external fun RenderNodeAnimationClock_nStart(
    ptr: NativePointer, node: NativePointer, property: Int, curve: InteropPointer, curveSize: Int,
    from: Float, target: Float, velocity: Float, startTimeNanos: Long, durationScale: Float, alphaCompositing: Int,
): Long

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nCancel")
// Needs ForceNativeThreadState: the last reference to a node the clock held can go.
// Native call path: RenderNodeAnimationClock::cancel -> ~RenderNode
@ForceNativeThreadState
private external fun RenderNodeAnimationClock_nCancel(ptr: NativePointer, node: NativePointer, property: Int)

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nSnap")
// Needs ForceNativeThreadState: sets a property (see nStart).
// Native call path: RenderNodeAnimationClock::snap -> RenderNode::setAlpha -> invalidateSnapshot
@ForceNativeThreadState
private external fun RenderNodeAnimationClock_nSnap(ptr: NativePointer, node: NativePointer, property: Int, value: Float, alphaCompositing: Int)

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nTick")
// Needs ForceNativeThreadState: sets the properties of every animated node (see nStart), and can release nodes whose
// animations ended.
// Native call path: RenderNodeAnimationClock::tick -> RenderNode::setAlpha -> invalidateSnapshot
@ForceNativeThreadState
private external fun RenderNodeAnimationClock_nTick(ptr: NativePointer, frameTimeNanos: Long): Boolean

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nQuery")
// No ForceNativeThreadState: reads a counter or looks an id up among a few animations.
// Native call path: RenderNodeAnimationClock::isActive, endedCount, isRunning
private external fun RenderNodeAnimationClock_nQuery(ptr: NativePointer, what: Int, id: Long): Long

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nNodeQuery")
// No ForceNativeThreadState: reads a stored value or evaluates a curve.
// Native call path: RenderNodeAnimationClock::value, velocity, valueAt
private external fun RenderNodeAnimationClock_nNodeQuery(ptr: NativePointer, node: NativePointer, property: Int, what: Int, time: Long): Float
