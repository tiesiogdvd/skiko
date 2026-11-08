package org.jetbrains.skiko.node

import org.jetbrains.skia.*
import org.jetbrains.skia.impl.*
import org.jetbrains.skia.impl.Library.Companion.staticLoad

/**
 * Which device pixels a frame of [RenderNode]s changes since the previous frame drawn through this tracker, so that a
 * host keeping the previous frame's pixels redraws only those (clipped to them) and keeps the rest.
 *
 * For each frame call [beginFrame], then [addNode] for every root node the frame draws, in drawing order and under the
 * transform it draws them with, then [endFrame], which returns the damage. The nodes are walked as they will be drawn:
 * a node whose own state (properties, layer paint, recorded content), transform or clip changed damages the device
 * area its drawing covers, before and after; a subtree nothing changed in is not walked again. Damage covers a layer
 * whole or not at all, so that the layer renders to the same pixels as in a full redraw, and a layer with an image
 * filter is damaged whole whenever anything inside it changes. The whole target is damaged on the first frame, after
 * a size change or [invalidate], and where the nodes draw something whose changes can't be tracked (a backdrop
 * layer, which reads what is under it). A drawable other than a render node is damaged on every frame.
 *
 * A tracker turns [RenderNodeContext.damageTracking] on for every context while it is [measuring] (the nodes'
 * recordings measure what they draw; one made before measured nothing, and the area its node draws counts as
 * unknown, the clip's), so make it before the nodes record. The nodes need the snapshot cache on (it relays changes upwards).
 * After a walk, drawing a node is skipped where the walk found it outside the canvas's clip, so a frame clipped to its
 * damage costs in proportion to it.
 *
 * The host has to draw every frame whose damage it took (clipped to at least that damage), or call [invalidate]: the
 * tracker takes the nodes' state at [endFrame] as drawn. Content the host draws besides the nodes is its own to
 * damage. Owned by one thread, like the nodes.
 */
class RenderNodeDamageTracker internal constructor(ptr: NativePointer, managed: Boolean = true) : RefCnt(ptr, managed) {
    private companion object {
        init {
            staticLoad()
        }
    }

    constructor() : this(RenderNodeDamageTracker_nMake()) {
        Stats.onNativeCall()
    }

    /** Starts a frame of a [width] x [height] target (a size change damages it whole). */
    fun beginFrame(width: Int, height: Int) {
        try {
            Stats.onNativeCall()
            RenderNodeDamageTracker_nBeginFrame(_ptr, width, height)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /** Walks [node], drawn next in this frame under [matrix] (the identity when null). */
    fun addNode(node: RenderNode, matrix: Matrix33? = null) {
        try {
            Stats.onNativeCall()
            interopScope {
                RenderNodeDamageTracker_nAddNode(_ptr, getPtr(node), toInterop((matrix ?: Matrix33.IDENTITY).mat))
            }
        } finally {
            reachabilityBarrier(node)
            reachabilityBarrier(this)
        }
    }

    /** Damages the whole target in the next frame: content drawn besides the nodes changed, or a frame wasn't drawn. */
    fun invalidate() {
        try {
            Stats.onNativeCall()
            RenderNodeDamageTracker_nInvalidate(_ptr)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /**
     * Whether the nodes' recordings measure what they draw for this tracker (true from the start; every context's
     * nodes measure while any tracker does). A host drawing whole frames for a while can turn it off to save the
     * measuring; what is recorded meanwhile counts as drawing everything inside its clip until recorded again.
     */
    var measuring: Boolean = true
        set(value) {
            field = value
            try {
                Stats.onNativeCall()
                RenderNodeDamageTracker_nSetMeasuring(_ptr, value)
            } finally {
                reachabilityBarrier(this)
            }
        }

    /**
     * Ends the frame: its damage, null when the whole target is damaged, else non-overlapping rects within the target
     * as left, top, right, bottom quadruples (empty when nothing changed).
     */
    fun endFrame(): IntArray? {
        return try {
            Stats.onNativeCall()
            val count = RenderNodeDamageTracker_nEndFrame(_ptr)
            if (count < 0) {
                null
            } else {
                interopScope {
                    withResult(IntArray(count * 4)) { RenderNodeDamageTracker_nGetRects(_ptr, it) }
                }
            }
        } finally {
            reachabilityBarrier(this)
        }
    }

    /** The nodes the last frame's walk visited, and how many of them it found unchanged with their subtree. */
    val walkedNodes: Int
        get() = try {
            Stats.onNativeCall()
            RenderNodeDamageTracker_nGetWalked(_ptr, false)
        } finally {
            reachabilityBarrier(this)
        }

    val skippedNodes: Int
        get() = try {
            Stats.onNativeCall()
            RenderNodeDamageTracker_nGetWalked(_ptr, true)
        } finally {
            reachabilityBarrier(this)
        }
}

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nMake")
// No ForceNativeThreadState: allocates an empty tracker, so it is short and cannot block.
// Native call path: sk_make_sp<RenderNodeDamageTracker>
private external fun RenderNodeDamageTracker_nMake(): NativePointer

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nBeginFrame")
// No ForceNativeThreadState: resets a few lists and allocates the walk's canvas, so it is short and cannot block.
// Native call path: RenderNodeDamageTracker::beginFrame
private external fun RenderNodeDamageTracker_nBeginFrame(ptr: NativePointer, width: Int, height: Int)

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nAddNode")
// Needs ForceNativeThreadState: replays the recordings of every changed node in the tree, so its time grows with the
// tree.
// Native call path: RenderNodeDamageTracker::addNode -> RenderNode::walkDamage
@ForceNativeThreadState
private external fun RenderNodeDamageTracker_nAddNode(ptr: NativePointer, node: NativePointer, matrix: InteropPointer)

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nInvalidate")
// No ForceNativeThreadState: sets a flag, so it is short and cannot block.
// Native call path: RenderNodeDamageTracker::invalidate
private external fun RenderNodeDamageTracker_nInvalidate(ptr: NativePointer)

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nEndFrame")
// Needs ForceNativeThreadState: merges the damage with every layer the frame draws, so its time grows with the tree.
// Native call path: RenderNodeDamageTracker::endFrame
@ForceNativeThreadState
private external fun RenderNodeDamageTracker_nEndFrame(ptr: NativePointer): Int

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nGetRects")
// No ForceNativeThreadState: copies at most a few rects, so it is short and cannot block.
// Native call path: RenderNodeDamageTracker::getRects
private external fun RenderNodeDamageTracker_nGetRects(ptr: NativePointer, result: InteropPointer)

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nGetWalked")
// No ForceNativeThreadState: reads a counter, so it is short and cannot block.
// Native call path: RenderNodeDamageTracker::walkedNodes, skippedNodes
private external fun RenderNodeDamageTracker_nGetWalked(ptr: NativePointer, skipped: Boolean): Int

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nSetMeasuring")
// No ForceNativeThreadState: updates a counter, so it is short and cannot block.
// Native call path: RenderNodeDamageTracker::setMeasuring
private external fun RenderNodeDamageTracker_nSetMeasuring(ptr: NativePointer, measuring: Boolean)
