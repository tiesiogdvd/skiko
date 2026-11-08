package org.jetbrains.skia.gpu.graphite

import org.jetbrains.skia.ExternalSymbolName
import org.jetbrains.skia.ForceNativeThreadState
import org.jetbrains.skia.impl.Managed
import org.jetbrains.skia.impl.NativePointer
import org.jetbrains.skia.impl.Stats
import org.jetbrains.skia.impl.reachabilityBarrier
import org.jetbrains.skiko.ExperimentalSkikoApi

/**
 * Provides the interface for recording drawing commands and snapping them into a [Recording].
 *
 * A recorder is intended to be used from one thread and is not thread-safe. Different recorders
 * can be used on different threads.
 */
@ExperimentalSkikoApi
class Recorder internal constructor(ptr: NativePointer) : Managed(ptr, _FinalizerHolder.PTR) {
    /**
     * Captures the drawing commands accumulated since the previous call to `snap`.
     *
     * The recorder is reset and ready to record new commands after this call.
     *
     * @return a recording containing the captured commands.
     */
    fun snap(): Recording {
        try {
            Stats.onNativeCall()
            val ptr = _nSnap(nativePtr)
            check(ptr != NullPointer) { "Failed to create a Graphite recording" }
            return Recording(ptr)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /**
     * Frees the recorder's GPU resources (its layers' textures, atlases, uploaded images) that no draw has used for
     * [msNotUsed] milliseconds, or that are marked for deletion, whether or not it is over budget. Resources the GPU
     * may still be reading stay. [maxPurgingMicros], when given, bounds how long the call spends purging.
     */
    fun performDeferredCleanup(msNotUsed: Long, maxPurgingMicros: Long? = null) {
        require(msNotUsed >= 0L) { "msNotUsed is negative" }
        require(maxPurgingMicros == null || maxPurgingMicros >= 0L) { "maxPurgingMicros is negative" }
        try {
            Stats.onNativeCall()
            _nRecorderPerformDeferredCleanup(nativePtr, msNotUsed, maxPurgingMicros ?: -1L)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /** Frees every GPU resource the recorder holds that the GPU isn't still reading. */
    fun freeGpuResources() {
        try {
            Stats.onNativeCall()
            _nRecorderFreeGpuResources(nativePtr)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /** Bytes of the recorder's GPU budget in use. */
    val currentBudgetedBytes: Long
        get() = try {
            Stats.onNativeCall()
            _nRecorderGetBudgetBytes(nativePtr, BUDGET_CURRENT)
        } finally {
            reachabilityBarrier(this)
        }

    /** Bytes of the recorder's resources that could be freed now (nothing reads them). */
    val currentPurgeableBytes: Long
        get() = try {
            Stats.onNativeCall()
            _nRecorderGetBudgetBytes(nativePtr, BUDGET_PURGEABLE)
        } finally {
            reachabilityBarrier(this)
        }

    /** The recorder's GPU budget in bytes; lowering it frees resources to get under it. */
    var maxBudgetedBytes: Long
        get() = try {
            Stats.onNativeCall()
            _nRecorderGetBudgetBytes(nativePtr, BUDGET_MAX)
        } finally {
            reachabilityBarrier(this)
        }
        set(value) {
            require(value >= 0L) { "maxBudgetedBytes is negative" }
            try {
                Stats.onNativeCall()
                _nSetMaxBudgetedBytes(nativePtr, value)
            } finally {
                reachabilityBarrier(this)
            }
        }

    private object _FinalizerHolder {
        val PTR = _nGetRecorderFinalizer()
    }
}

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_Recorder__1nGetFinalizer")
// No ForceNativeThreadState: returns the function that frees the object, without calling it, so it is short and cannot
// block.
// Native call path: deleteRecorder
private external fun _nGetRecorderFinalizer(): NativePointer

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_Recorder__1nSnap")
// Needs ForceNativeThreadState: finishes pending drawing work and prepares resources through Metal.
// Native call path: Recorder::snap -> flushTrackedDevices -> prepareResources
@ForceNativeThreadState
private external fun _nSnap(recorderPtr: NativePointer): NativePointer

/** Which of a resource cache's byte counts `_nGetBudgetBytes` reads (Recorder and GraphiteContext). */
internal const val BUDGET_CURRENT = 0
internal const val BUDGET_PURGEABLE = 1
internal const val BUDGET_MAX = 2

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_Recorder__1nPerformDeferredCleanup")
// Needs ForceNativeThreadState: frees Metal resources through the resource cache.
// Native call path: Recorder::performDeferredCleanup -> ResourceCache::purgeResourcesNotUsedSince
@ForceNativeThreadState
private external fun _nRecorderPerformDeferredCleanup(recorderPtr: NativePointer, msNotUsed: Long, maxPurgingMicros: Long)

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_Recorder__1nFreeGpuResources")
// Needs ForceNativeThreadState: frees Metal resources through the resource cache.
// Native call path: Recorder::freeGpuResources -> ResourceCache::purgeResources
@ForceNativeThreadState
private external fun _nRecorderFreeGpuResources(recorderPtr: NativePointer)

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_Recorder__1nGetBudgetBytes")
// No ForceNativeThreadState: reads a counter of the resource cache, so it is short and cannot block.
private external fun _nRecorderGetBudgetBytes(recorderPtr: NativePointer, which: Int): Long

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_Recorder__1nSetMaxBudgetedBytes")
// Needs ForceNativeThreadState: lowering the budget frees Metal resources through the resource cache.
// Native call path: Recorder::setMaxBudgetedBytes -> ResourceCache::setMaxBudget -> purgeAsNeeded
@ForceNativeThreadState
private external fun _nSetMaxBudgetedBytes(recorderPtr: NativePointer, bytes: Long)
