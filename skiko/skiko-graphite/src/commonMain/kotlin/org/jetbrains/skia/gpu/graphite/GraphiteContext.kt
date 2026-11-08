package org.jetbrains.skia.gpu.graphite

import org.jetbrains.skia.ExternalSymbolName
import org.jetbrains.skia.ForceNativeThreadState
import org.jetbrains.skia.impl.InteropPointer
import org.jetbrains.skia.impl.Managed
import org.jetbrains.skia.impl.Native.Companion.NullPointer
import org.jetbrains.skia.impl.NativePointer
import org.jetbrains.skia.impl.NativePointerArray
import org.jetbrains.skia.impl.Stats
import org.jetbrains.skia.impl.interopScope
import org.jetbrains.skia.impl.reachabilityBarrier
import org.jetbrains.skiko.ExperimentalSkikoApi

/**
 * The main entry point for Graphite, responsible for managing and coordinating GPU resources.
 */
@ExperimentalSkikoApi
class GraphiteContext internal constructor(ptr: NativePointer) : Managed(ptr, _FinalizerHolder.PTR) {
    companion object {
        init {
            GraphiteLibrary.load()
        }

        /**
         * Creates a Graphite context that submits work to a Metal command queue.
         *
         * @param devicePtr native pointer to the Metal device.
         * @param queuePtr native pointer to the Metal command queue.
         * @param options the context's options; the defaults are Skia's.
         * @return a Graphite context backed by Metal.
         */
        fun makeMetal(
            devicePtr: NativePointer,
            queuePtr: NativePointer,
            options: GraphiteContextOptions = GraphiteContextOptions(),
        ): GraphiteContext {
            requireMetalSupport()
            require(devicePtr != NullPointer) { "Metal device pointer is null" }
            require(queuePtr != NullPointer) { "Metal queue pointer is null" }
            Stats.onNativeCall()
            val ptr = _nMakeMetal(
                devicePtr,
                queuePtr,
                options.internalMultisampleCount,
                options.minimumPathSizeForMsaa,
                options.glyphCacheTextureMaximumBytes,
                options.setBackendLabels,
            )
            check(ptr != NullPointer) { "Failed to create a Graphite Metal context" }
            return GraphiteContext(ptr)
        }

        /**
         * Creates a Graphite context that submits work to a Vulkan queue.
         *
         * The Vulkan objects are owned by the caller and must outlive the returned context.
         *
         * @param instancePtr native pointer to the `VkInstance`.
         * @param physicalDevicePtr native pointer to the `VkPhysicalDevice`.
         * @param devicePtr native pointer to the `VkDevice`.
         * @param queuePtr native pointer to the `VkQueue` the work is submitted to.
         * @param graphicsQueueIndex index of the queue family [queuePtr] belongs to; it must
         * support graphics operations.
         * @param maxApiVersion highest Vulkan API version (`VK_MAKE_API_VERSION`) Skia may use.
         * @return a Graphite context backed by Vulkan.
         */
        fun makeVulkan(
            instancePtr: NativePointer,
            physicalDevicePtr: NativePointer,
            devicePtr: NativePointer,
            queuePtr: NativePointer,
            graphicsQueueIndex: Int,
            maxApiVersion: Int,
        ): GraphiteContext {
            requireVulkanSupport()
            require(instancePtr != NullPointer) { "Vulkan instance pointer is null" }
            require(physicalDevicePtr != NullPointer) { "Vulkan physical device pointer is null" }
            require(devicePtr != NullPointer) { "Vulkan device pointer is null" }
            require(queuePtr != NullPointer) { "Vulkan queue pointer is null" }
            Stats.onNativeCall()
            val ptr = _nMakeVulkan(
                instancePtr,
                physicalDevicePtr,
                devicePtr,
                queuePtr,
                graphicsQueueIndex,
                maxApiVersion,
            )
            check(ptr != NullPointer) { "Failed to create a Graphite Vulkan context" }
            return GraphiteContext(ptr)
        }
    }

    /**
     * Creates a [Recorder] that records drawing commands for this context.
     *
     * @param gpuBudgetInBytes the budget of the recorder's own GPU resources (the textures of its layers, its atlases,
     * the images it uploads), over which it frees purgeable ones; null keeps Skia's default (256 MB).
     * @return a new recorder.
     */
    fun makeRecorder(gpuBudgetInBytes: Long? = null): Recorder {
        require(gpuBudgetInBytes == null || gpuBudgetInBytes >= 0L) { "GPU budget is negative" }
        return try {
            Stats.onNativeCall()
            val ptr = _nMakeRecorder(nativePtr, gpuBudgetInBytes ?: -1L)
            check(ptr != NullPointer) { "Failed to create a Graphite recorder" }
            Recorder(ptr)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /**
     * Adds a [recording] to this context's pending GPU work.
     *
     * The work is sent to the GPU by a subsequent call to [submit].
     *
     * @param recording recording to insert.
     */
    fun insertRecording(recording: Recording) {
        insertRecording(InsertRecordingInfo(recording))
    }

    /**
     * Adds a recording and associated submission metadata (such as wait/signal semaphores)
     * to this context's pending GPU work.
     *
     * @param info recording insertion parameters.
     */
    fun insertRecording(info: InsertRecordingInfo) {
        try {
            Stats.onNativeCall()
            val waitSemPtrs = NativePointerArray(info.waitSemaphores.size)
            val signalSemPtrs = NativePointerArray(info.signalSemaphores.size)
            for (index in info.waitSemaphores.indices) {
                val semaphore = info.waitSemaphores[index]
                require(!semaphore.isClosed) { "Wait semaphore is closed" }
                waitSemPtrs[index] = semaphore.nativePtr
            }
            for (index in info.signalSemaphores.indices) {
                val semaphore = info.signalSemaphores[index]
                require(!semaphore.isClosed) { "Signal semaphore is closed" }
                signalSemPtrs[index] = semaphore.nativePtr
            }
            interopScope {
                _nInsertRecording(
                    nativePtr,
                    info.recording.nativePtr,
                    toInterop(waitSemPtrs),
                    info.waitSemaphores.size,
                    toInterop(signalSemPtrs),
                    info.signalSemaphores.size,
                )
            }
        } finally {
            reachabilityBarrier(this)
            reachabilityBarrier(info.recording)
            for (sem in info.waitSemaphores) {
                reachabilityBarrier(sem)
            }
            for (sem in info.signalSemaphores) {
                reachabilityBarrier(sem)
            }
        }
    }

    /**
     * Submits pending work to the GPU.
     *
     * @param syncCpu if `true`, waits for the submitted GPU work to finish before returning.
     */
    fun submit(syncCpu: Boolean = false) {
        try {
            Stats.onNativeCall()
            _nSubmit(nativePtr, syncCpu)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /**
     * Frees the context's GPU resources that no work has used for [msNotUsed] milliseconds, or that are marked for
     * deletion, and handles finished GPU work. A [Recorder] keeps its own resources: see
     * [Recorder.performDeferredCleanup]. [maxPurgingMicros], when given, bounds how long the call spends purging.
     */
    fun performDeferredCleanup(msNotUsed: Long, maxPurgingMicros: Long? = null) {
        require(msNotUsed >= 0L) { "msNotUsed is negative" }
        require(maxPurgingMicros == null || maxPurgingMicros >= 0L) { "maxPurgingMicros is negative" }
        try {
            Stats.onNativeCall()
            _nPerformDeferredCleanup(nativePtr, msNotUsed, maxPurgingMicros ?: -1L)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /** Frees every GPU resource the context holds that the GPU isn't still reading. */
    fun freeGpuResources() {
        try {
            Stats.onNativeCall()
            _nFreeGpuResources(nativePtr)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /** Bytes of the context's GPU budget in use (its own cache, not its recorders'). */
    val currentBudgetedBytes: Long
        get() = try {
            Stats.onNativeCall()
            _nGetBudgetBytes(nativePtr, BUDGET_CURRENT)
        } finally {
            reachabilityBarrier(this)
        }

    /** Bytes of the context's resources that could be freed now. */
    val currentPurgeableBytes: Long
        get() = try {
            Stats.onNativeCall()
            _nGetBudgetBytes(nativePtr, BUDGET_PURGEABLE)
        } finally {
            reachabilityBarrier(this)
        }

    private object _FinalizerHolder {
        val PTR = _nGetGraphiteContextFinalizer()
    }
}

/**
 * Options of a [GraphiteContext]: a subset of Skia's `skgpu::graphite::ContextOptions`. The defaults are Skia's.
 *
 * @property internalMultisampleCount the most samples Graphite uses for its own MSAA draws (paths drawn by
 * tessellation, for one); 1 turns those code paths off (paths go to the atlas).
 * @property minimumPathSizeForMsaa paths smaller than this (device pixels) are drawn through the path atlas instead
 * of MSAA; 0 draws every path that needs coverage with MSAA where it is on.
 * @property glyphCacheTextureMaximumBytes the most bytes of the glyph cache's textures; 0 keeps Skia's default.
 * @property setBackendLabels whether Graphite labels its backend objects (textures, buffers, pipelines), for GPU
 * tools; off by default in a release build of Skia.
 */
@ExperimentalSkikoApi
class GraphiteContextOptions(
    val internalMultisampleCount: Int = 4,
    val minimumPathSizeForMsaa: Float = 0f,
    val glyphCacheTextureMaximumBytes: Long = 0L,
    val setBackendLabels: Boolean = false,
) {
    init {
        require(internalMultisampleCount in listOf(1, 2, 4, 8, 16)) {
            "internalMultisampleCount must be 1, 2, 4, 8 or 16, not $internalMultisampleCount"
        }
        require(minimumPathSizeForMsaa >= 0f) { "minimumPathSizeForMsaa is negative" }
        require(glyphCacheTextureMaximumBytes >= 0L) { "glyphCacheTextureMaximumBytes is negative" }
    }
}

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_GraphiteContext__1nGetFinalizer")
// No ForceNativeThreadState: returns the function that frees the object, without calling it, so it is short and cannot
// block.
// Native call path: deleteGraphiteContext
private external fun _nGetGraphiteContextFinalizer(): NativePointer

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_GraphiteContext__1nMakeMetal")
// Needs ForceNativeThreadState: retains a Metal device and command queue, then builds the Metal context.
// Native call path: ContextFactory::MakeMetal -> MtlSharedContext::Make
@ForceNativeThreadState
private external fun _nMakeMetal(
    devicePtr: NativePointer,
    queuePtr: NativePointer,
    internalMultisampleCount: Int,
    minimumPathSizeForMsaa: Float,
    glyphCacheTextureMaximumBytes: Long,
    setBackendLabels: Boolean,
): NativePointer

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_GraphiteContext__1nMakeVulkan")
// No ForceNativeThreadState: returns 0 and ignores all six arguments, so it is short and cannot block.
private external fun _nMakeVulkan(
    instancePtr: NativePointer,
    physicalDevicePtr: NativePointer,
    devicePtr: NativePointer,
    queuePtr: NativePointer,
    graphicsQueueIndex: Int,
    maxApiVersion: Int,
): NativePointer

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_GraphiteContext__1nMakeRecorder")
// Needs ForceNativeThreadState: creates a recorder, and its resource provider creates Metal objects.
// Native call path: Context::makeRecorder -> Recorder::Recorder
@ForceNativeThreadState
private external fun _nMakeRecorder(contextPtr: NativePointer, gpuBudgetInBytes: Long): NativePointer

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_GraphiteContext__1nInsertRecording")
// Needs ForceNativeThreadState: queues GPU work, which is recorded as a Metal command buffer.
// Native call path: Context::insertRecording -> QueueManager::addRecording
@ForceNativeThreadState
private external fun _nInsertRecording(
    contextPtr: NativePointer,
    recordingPtr: NativePointer,
    waitSemaphoresPtrs: InteropPointer,
    waitSemaphoresCount: Int,
    signalSemaphoresPtrs: InteropPointer,
    signalSemaphoresCount: Int,
)

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_GraphiteContext__1nSubmit")
// Needs ForceNativeThreadState: commits a Metal command buffer to the GPU and can wait for the GPU to sync back to the
// CPU.
// Native call path: Context::submit -> QueueManager::submitToGpu -> MtlCommandBuffer::commit
@ForceNativeThreadState
private external fun _nSubmit(contextPtr: NativePointer, syncCpu: Boolean)

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_GraphiteContext__1nPerformDeferredCleanup")
// Needs ForceNativeThreadState: checks finished GPU work and frees Metal resources through the resource cache.
// Native call path: Context::performDeferredCleanup -> ResourceCache::purgeResourcesNotUsedSince
@ForceNativeThreadState
private external fun _nPerformDeferredCleanup(contextPtr: NativePointer, msNotUsed: Long, maxPurgingMicros: Long)

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_GraphiteContext__1nFreeGpuResources")
// Needs ForceNativeThreadState: checks finished GPU work and frees Metal resources through the resource cache.
// Native call path: Context::freeGpuResources -> ResourceCache::purgeResources
@ForceNativeThreadState
private external fun _nFreeGpuResources(contextPtr: NativePointer)

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_GraphiteContext__1nGetBudgetBytes")
// No ForceNativeThreadState: reads a counter of the resource cache, so it is short and cannot block.
private external fun _nGetBudgetBytes(contextPtr: NativePointer, which: Int): Long
