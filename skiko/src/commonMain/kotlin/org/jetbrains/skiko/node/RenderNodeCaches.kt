package org.jetbrains.skiko.node

import org.jetbrains.skia.ExternalSymbolName
import org.jetbrains.skia.impl.Library.Companion.staticLoad
import org.jetbrains.skia.impl.Stats

/**
 * The textures [RenderNode]s keep between frames, on GPU canvases (Ganesh and Graphite): a layer whose content and
 * placement stay the same is rendered once and only composited after (a retained layer); a clipped subtree drawn
 * unchanged for a few frames is drawn as one image (the raster cache); with `SKIKO_RENDER_NODE_PROMOTE=1`, a layer or
 * clipped subtree whose transform changes while its content doesn't is rendered once at a fixed raster transform and
 * composited, filtered, at each frame's transform (a promotion, for a motion's length: a [RenderNodeAnimationClock]'s
 * animation, or two frames of motion in a row on a host that draws settle frames). Textures are budgeted together
 * (`SKIKO_RENDER_NODE_LAYER_RETAIN_MB`, 96 by default), and one not drawn for a while
 * (`SKIKO_RENDER_NODE_CACHE_MAX_AGE_MS`, 5000 by default) is freed. A Graphite recorder's or a Ganesh context's
 * `performDeferredCleanup` and `freeGpuResources` free its textures that went unused for as long, and a recorder
 * going away frees its textures. `SKIKO_RENDER_NODE_RASTER_CACHE=0|1` turns the raster cache off or on for Graphite too
 * (Ganesh only by default), `SKIKO_RENDER_NODE_LAYER_RETAIN=0|plain|1` the retained layers (image filter layers are
 * Ganesh's only by default).
 *
 * The state here is per thread, the thread that draws the nodes (a render thread drawing [RenderNodeTwins]): there,
 * [beginFrame] at the start of each frame (before a [RenderNodeDamageTracker]'s walk, where it walks on that thread);
 * after the frame, while [settleRequested], one more frame, in which the nodes whose guessed motion ended are drawn
 * exactly again; [purge] and [drawWithoutCaches] there too.
 */
object RenderNodeCaches {
    init {
        staticLoad()
    }

    /**
     * A frame starts on this thread: the frame's draws of a node (one per damaged rect) count once, the nodes that came
     * to rest since they were last composited from a promoted texture are marked changed, and textures unused for long
     * are freed.
     */
    fun beginFrame() {
        Stats.onNativeCall()
        RenderNodeCaches_nBeginFrame()
    }

    /**
     * Whether the frame since [beginFrame] composited a promoted texture at a transform it doesn't reproduce exactly:
     * the host draws one more frame then (the textures are drawn exactly once the motion stops).
     */
    val settleRequested: Boolean
        get() {
            Stats.onNativeCall()
            return RenderNodeCaches_nSettleRequested()
        }

    /** The GPU memory the textures hold, in bytes (every thread's). */
    val bytes: Long
        get() {
            Stats.onNativeCall()
            return RenderNodeCaches_nGetBytes()
        }

    /** How many textures are kept (every thread's). */
    val count: Int
        get() {
            Stats.onNativeCall()
            return RenderNodeCaches_nGetCount()
        }

    /** Frees this thread's textures not drawn for [msNotUsed] milliseconds (all of them for 0). */
    fun purge(msNotUsed: Long = 0L) {
        require(msNotUsed >= 0L) { "msNotUsed is negative" }
        Stats.onNativeCall()
        RenderNodeCaches_nPurge(msNotUsed)
    }

    /**
     * While set, nodes drawn on this thread skip the textures and their bookkeeping: a host's reference drawing (a whole
     * frame drawn to check a damage-clipped one) draws what the nodes would draw without them.
     */
    fun drawWithoutCaches(block: () -> Unit) {
        Stats.onNativeCall()
        RenderNodeCaches_nSetBypass(true)
        try {
            block()
        } finally {
            RenderNodeCaches_nSetBypass(false)
        }
    }
}

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nBeginFrame")
// No ForceNativeThreadState: marks the nodes that came to rest, and frees a few textures at most twice a second.
// Native call path: RasterCache::beginFrame
private external fun RenderNodeCaches_nBeginFrame()

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nSettleRequested")
// No ForceNativeThreadState: reads a counter.
// Native call path: RasterCache::settleRequested
private external fun RenderNodeCaches_nSettleRequested(): Boolean

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nGetBytes")
// No ForceNativeThreadState: reads a counter.
// Native call path: RasterCache::bytes
private external fun RenderNodeCaches_nGetBytes(): Long

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nGetCount")
// No ForceNativeThreadState: reads a counter.
// Native call path: RasterCache::count
private external fun RenderNodeCaches_nGetCount(): Int

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nPurge")
// No ForceNativeThreadState: releases at most the textures kept, which is quick.
// Native call path: RasterCache::purge
private external fun RenderNodeCaches_nPurge(msNotUsed: Long)

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nSetBypass")
// No ForceNativeThreadState: sets a flag.
// Native call path: RasterCache::setBypass
private external fun RenderNodeCaches_nSetBypass(bypass: Boolean)
