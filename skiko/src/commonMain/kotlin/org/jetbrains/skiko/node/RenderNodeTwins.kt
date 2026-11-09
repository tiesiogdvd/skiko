package org.jetbrains.skiko.node

import org.jetbrains.skia.*
import org.jetbrains.skia.impl.*
import org.jetbrains.skia.impl.Library.Companion.staticLoad

/**
 * Render twins: copies of [RenderNode]s that another thread draws, as Android's RenderThread draws its own copy of the
 * UI thread's display lists and properties (HWUI's staging and render properties, brought over in its sync stage).
 *
 * On the nodes' thread a frame is recorded between [beginRecording] and [finishRecording], the render nodes it draws
 * recorded by reference ([RenderNodeFrame] draws the live nodes). [sync] then gives the same frame drawing each node's
 * twin instead, bringing the twins up to date on the way: a node's properties, its recorded content (where it draws
 * other nodes, recorded anew with their twins, otherwise the same immutable recording, shared) and the nodes it draws.
 * With the snapshot cache on ([RenderNodeContext]), changes relay upwards, so a sync visits only the nodes that changed
 * since the last one and the nodes drawing them. The twins are render nodes of their own, so where they are drawn the
 * layer atlas, retained layers and live snapshots apply as to any node.
 *
 * Threads: [sync] reads the nodes, so their thread must not change them meanwhile (it may run on another thread while
 * the nodes' thread waits for it); it changes the twins, so no frame it returned may be drawn meanwhile. A frame [sync]
 * returned stays valid until the next [sync]; draw it on one thread at a time. A node that goes away hands its twin
 * back; [releasePending] releases what was handed back, on the thread that draws the twins (a twin may keep GPU
 * textures made there), and [close] does too (later twins go at once).
 */
class RenderNodeTwins internal constructor(ptr: NativePointer) : RefCnt(ptr) {
    private companion object {
        init {
            staticLoad()
        }
    }

    constructor() : this(RenderNodeTwins_nMake()) {
        Stats.onNativeCall()
    }

    /** Starts recording a frame of [width] x [height] (the nodes' thread); the canvas is valid until [finishRecording]. */
    fun beginRecording(width: Float, height: Float): Canvas {
        return try {
            Stats.onNativeCall()
            Canvas(RenderNodeTwins_nBeginRecording(_ptr, width, height), managed = false, _owner = this)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /** The frame as recorded: it draws the nodes themselves, so only on their thread (or while it waits). */
    fun finishRecording(): RenderNodeFrame {
        return try {
            Stats.onNativeCall()
            RenderNodeFrame(RenderNodeTwins_nFinishRecording(_ptr))
        } finally {
            reachabilityBarrier(this)
        }
    }

    /** [frame] (from [finishRecording]) drawing the twins of its nodes, the twins brought up to date (see above). */
    fun sync(frame: RenderNodeFrame): RenderNodeFrame {
        return try {
            Stats.onNativeCall()
            RenderNodeFrame(RenderNodeTwins_nSync(_ptr, getPtr(frame)))
        } finally {
            reachabilityBarrier(frame)
            reachabilityBarrier(this)
        }
    }

    /** Releases the twins of the nodes that went away since the last call: on the thread drawing the twins. */
    fun releasePending() {
        try {
            Stats.onNativeCall()
            RenderNodeTwins_nReleasePending(_ptr)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /** The nodes the last [sync] visited, and how many of them it recorded anew, for logs. */
    val visitedNodes: Int
        get() = try {
            Stats.onNativeCall()
            RenderNodeTwins_nGetCount(_ptr, false)
        } finally {
            reachabilityBarrier(this)
        }

    val recordedNodes: Int
        get() = try {
            Stats.onNativeCall()
            RenderNodeTwins_nGetCount(_ptr, true)
        } finally {
            reachabilityBarrier(this)
        }

    /** Releases what is pending (on the thread drawing the twins); twins handed back later are released at once. */
    override fun close() {
        if (!isClosed) {
            try {
                Stats.onNativeCall()
                RenderNodeTwins_nClose(_ptr)
            } finally {
                reachabilityBarrier(this)
            }
        }
        super.close()
    }
}

/** A frame recorded through [RenderNodeTwins]: immutable, it draws the nodes (or the twins) it was recorded with. */
class RenderNodeFrame internal constructor(ptr: NativePointer) : RefCnt(ptr) {
    private companion object {
        init {
            staticLoad()
        }
    }

    /** Draws the frame onto [canvas]. */
    fun draw(canvas: Canvas) {
        try {
            Stats.onNativeCall()
            RenderNodeFrame_nDraw(_ptr, getPtr(canvas))
        } finally {
            reachabilityBarrier(canvas)
            reachabilityBarrier(this)
        }
    }
}

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nMake")
// No ForceNativeThreadState: allocates an empty object, so it is short and cannot block.
// Native call path: sk_make_sp<RenderNodeTwins>
private external fun RenderNodeTwins_nMake(): NativePointer

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nBeginRecording")
// Needs ForceNativeThreadState: starting a recording drops what an unfinished earlier one referenced, and the last
// reference released can free a typeface, image or Kotlin callback.
// Native call path: RenderNodeTwins::beginRecording -> SkPictureRecorder::beginRecording
@ForceNativeThreadState
private external fun RenderNodeTwins_nBeginRecording(ptr: NativePointer, width: Float, height: Float): NativePointer

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nFinishRecording")
// Needs ForceNativeThreadState: optimizes the recorded operations, so its time grows with them.
// Native call path: RenderNodeTwins::finishRecording -> SkPictureRecorder::finishRecordingAsDrawable
@ForceNativeThreadState
private external fun RenderNodeTwins_nFinishRecording(ptr: NativePointer): NativePointer

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nSync")
// Needs ForceNativeThreadState: replays the frame and the changed nodes' recordings, so its time grows with them, and
// replaced recordings can free images and typefaces.
// Native call path: RenderNodeTwins::sync -> RenderNode::syncTwin
@ForceNativeThreadState
private external fun RenderNodeTwins_nSync(ptr: NativePointer, frame: NativePointer): NativePointer

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nReleasePending")
// Needs ForceNativeThreadState: releasing twins frees their recordings and GPU textures.
// Native call path: RenderNodeTwins::releasePending
@ForceNativeThreadState
private external fun RenderNodeTwins_nReleasePending(ptr: NativePointer)

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nClose")
// Needs ForceNativeThreadState: releasing twins frees their recordings and GPU textures.
// Native call path: RenderNodeTwins::close
@ForceNativeThreadState
private external fun RenderNodeTwins_nClose(ptr: NativePointer)

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nGetCount")
// No ForceNativeThreadState: reads a counter, so it is short and cannot block.
// Native call path: RenderNodeTwins::visitedNodes, recordedNodes
private external fun RenderNodeTwins_nGetCount(ptr: NativePointer, recorded: Boolean): Int

@ExternalSymbolName("org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeFrame_1nDraw")
// Needs ForceNativeThreadState: draws every node of the frame onto a canvas that can be GPU-backed.
// Native call path: SkCanvas::drawDrawable -> RenderNode::onDraw
@ForceNativeThreadState
private external fun RenderNodeFrame_nDraw(ptr: NativePointer, canvas: NativePointer)
