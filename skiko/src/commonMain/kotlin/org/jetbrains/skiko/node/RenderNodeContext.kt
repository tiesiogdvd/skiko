package org.jetbrains.skiko.node

import org.jetbrains.skia.*
import org.jetbrains.skia.impl.*
import org.jetbrains.skia.impl.Library.Companion.staticLoad

class RenderNodeContext internal constructor(ptr: NativePointer, managed: Boolean = true) : RefCnt(ptr, managed) {
    private companion object {
        init {
            staticLoad()
        }
    }

    /**
     * @param measureDrawBounds whether the nodes created with this context measure the area their
     * content actually covers as they record it. Without it a node whose content is not confined to
     * its own bounds -- one that does not clip, or that casts a shadow -- reports a conservative
     * area instead, and whatever records that node widens its cull rect to match. It costs a
     * bounding hierarchy built over every recording.
     * @param snapshotCache whether the nodes created with this context record their content into an
     * immutable picture once and replay it until the content changes, instead of re-recording it for
     * every frame. It trades memory for recording time, so it pays off for trees that are drawn far
     * more often than they change, and costs for trees that are re-recorded every frame.
     *
     * Snapshotting is a property of the whole tree rather than of a single node: a snapshot inlines
     * the drawing of the nodes below it, so those nodes have to report their changes upwards for it
     * to be dropped in time. A node that keeps changing after a snapshot inlined it (an animated
     * one), one that casts a shadow (which depends on where it is replayed), one with a layer, and
     * one drawing other nodes under a modulated alpha are referenced from the snapshots instead and
     * drawn live, so their changes leave the snapshots above them valid.
     *
     * On a GPU canvas the nodes also draw their layers without a render pass each: the layers a
     * drawing reaches outside other layers are rendered together into one atlas texture first, and
     * a layer whose content and placement stay the same from one frame to the next keeps its texture
     * (an image filter layer, its filtered result) and is only composited again. Both reproduce
     * saveLayer's pixels. `SKIKO_RENDER_NODE_LAYER_ATLAS=0` and `SKIKO_RENDER_NODE_LAYER_RETAIN=0` in
     * the environment turn them off, `SKIKO_RENDER_NODE_LAYER_RETAIN_MB` bounds the retained
     * textures (96 by default), and `SKIKO_RENDER_NODE_LAYER_ATLAS=log` reports what they do.
     */
    constructor(
        measureDrawBounds: Boolean = false,
        snapshotCache: Boolean = true,
    ) : this(RenderNodeContext_nMake(measureDrawBounds, snapshotCache)) {
        Stats.onNativeCall()
    }

    /**
     * Whether the nodes measure what their recordings draw, for a [RenderNodeDamageTracker] (which needs it, and the
     * snapshot cache). It applies to the recordings made after it is set, so set it before the nodes record. Every
     * context measures once the process made a tracker (a host tracks nodes that a framework's contexts record).
     */
    var damageTracking: Boolean = false
        set(value) {
            field = value
            try {
                Stats.onNativeCall()
                RenderNodeContext_nSetDamageTracking(_ptr, value)
            } finally {
                reachabilityBarrier(this)
            }
        }

    fun setLightingInfo(
        centerX: Float = Float.MIN_VALUE,
        centerY: Float = Float.MIN_VALUE,
        centerZ: Float = Float.MIN_VALUE,
        radius: Float = 0f,
        ambientShadowAlpha: Float = 0f,
        spotShadowAlpha: Float = 0f
    ) {
        try {
            Stats.onNativeCall()
            RenderNodeContext_nSetLightingInfo(
                ptr = _ptr,
                centerX = centerX,
                centerY = centerY,
                centerZ = centerZ,
                radius = radius,
                ambientShadowAlpha = ambientShadowAlpha,
                spotShadowAlpha = spotShadowAlpha
            )
        } finally {
            reachabilityBarrier(this)
        }
    }
}

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeContextKt_RenderNodeContext_1nMake")
// No ForceNativeThreadState: allocates a small ref-counted context holding plain numbers and two flags, so it is short
// and cannot block.
// Native call path: sk_make_sp<RenderNodeContext>
private external fun RenderNodeContext_nMake(measureDrawBounds: Boolean, snapshotCache: Boolean): NativePointer

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeContextKt_RenderNodeContext_1nSetLightingInfo")
// No ForceNativeThreadState: copies six numbers into stored values, so it is short and cannot block.
// Native call path: RenderNodeContext::setLightingInfo
private external fun RenderNodeContext_nSetLightingInfo(ptr: NativePointer, centerX: Float, centerY: Float, centerZ: Float, radius: Float, ambientShadowAlpha: Float, spotShadowAlpha: Float)

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeContextKt_RenderNodeContext_1nSetDamageTracking")
// No ForceNativeThreadState: stores a flag, so it is short and cannot block.
// Native call path: RenderNodeContext::setTrackDamage
private external fun RenderNodeContext_nSetDamageTracking(ptr: NativePointer, damageTracking: Boolean)
