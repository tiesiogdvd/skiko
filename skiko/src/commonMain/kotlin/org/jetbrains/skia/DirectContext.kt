package org.jetbrains.skia

import org.jetbrains.skia.impl.*
import org.jetbrains.skia.impl.Library.Companion.staticLoad
import org.jetbrains.skia.impl.Native.Companion.NullPointer
import org.jetbrains.skiko.ExperimentalSkikoApi
import org.jetbrains.skiko.RenderException
import org.jetbrains.skiko.loadOpenGLLibrary

class DirectContext internal constructor(ptr: NativePointer, managed: Boolean = true) : RefCnt(ptr, managed) {
    companion object {
        fun makeGL(): DirectContext {
            Stats.onNativeCall()
            loadOpenGLLibrary()
            val ptr = _nMakeGL()
            if (ptr == NullPointer) throw RenderException("Can't create OpenGL DirectContext")
            return DirectContext(ptr)
        }

        fun makeMetal(devicePtr: NativePointer, queuePtr: NativePointer): DirectContext {
            Stats.onNativeCall()
            return DirectContext(_nMakeMetal(devicePtr, queuePtr))
        }

        /**
         * A Metal context whose shader programs persist in [shaderCacheDirectory] (Ganesh's persistent cache): a
         * program the context builds is stored there (its Metal source, one file a program), and a later context, in
         * this process or the next, loads it instead of translating it again, so a new effect's first frame (a blur, a
         * gradient) doesn't stall on every launch. The directory is made if missing; contexts may share it.
         */
        fun makeMetal(devicePtr: NativePointer, queuePtr: NativePointer, shaderCacheDirectory: String): DirectContext {
            Stats.onNativeCall()
            val ptr = interopScope { _nMakeMetalWithShaderCache(devicePtr, queuePtr, toInterop(shaderCacheDirectory)) }
            if (ptr == NullPointer) throw RenderException("Can't create Metal DirectContext")
            return DirectContext(ptr)
        }

        /**
         *
         * Creates Direct3D direct rendering context from D3D12 native objects.
         *
         * For more information refer to skia GrDirectContext class.
         *
         * @param adapterPtr    pointer to IDXGIAdapter1 object; must be not zero
         * @param devicePtr     pointer to ID3D12Device object, which is created with
         * provided adapter in adapterPtr; must be not zero
         * @param queuePtr      Pointer to ID3D12CommandQueue object, which
         * is created with provided device in devicePtr with
         * type D3D12_COMMAND_LIST_TYPE_DIRECT; must be not zero
         */
        fun makeDirect3D(adapterPtr: NativePointer, devicePtr: NativePointer, queuePtr: NativePointer): DirectContext {
            Stats.onNativeCall()
            return DirectContext(_nMakeDirect3D(adapterPtr, devicePtr, queuePtr))
        }

        /**
         * Creates Vulkan direct rendering context from Vulkan native objects.
         *
         * @param instancePtr pointer to VkInstance; must not be null
         * @param physicalDevicePtr pointer to VkPhysicalDevice; must not be null
         * @param devicePtr pointer to VkDevice; must not be null
         * @param queuePtr pointer to VkQueue; must not be null
         * @param graphicsQueueIndex index of the graphics queue used for rendering
         * @param instanceProcAddr pointer to vkGetInstanceProcAddr; must not be null
         * @param deviceProcAddr pointer to vkGetDeviceProcAddr; must not be null
         * @param apiVersion Vulkan API version
         * @param memoryAllocator custom allocator for Vulkan device memory, or null to use
         *                        Skia's built-in allocator
         */
        @ExperimentalSkikoApi
        fun makeVulkan(
            instancePtr: NativePointer,
            physicalDevicePtr: NativePointer,
            devicePtr: NativePointer,
            queuePtr: NativePointer,
            graphicsQueueIndex: Int,
            instanceProcAddr: NativePointer,
            deviceProcAddr: NativePointer,
            apiVersion: Int,
            memoryAllocator: VulkanMemoryAllocator? = null
        ): DirectContext {
            require(instancePtr != NullPointer) { "instancePtr must not be null" }
            require(physicalDevicePtr != NullPointer) { "physicalDevicePtr must not be null" }
            require(devicePtr != NullPointer) { "devicePtr must not be null" }
            require(queuePtr != NullPointer) { "queuePtr must not be null" }
            require(graphicsQueueIndex >= 0) { "graphicsQueueIndex must be non-negative" }
            require(instanceProcAddr != NullPointer) { "instanceProcAddr must not be null" }
            require(deviceProcAddr != NullPointer) { "deviceProcAddr must not be null" }

            Stats.onNativeCall()
            val ptr = nMakeVulkanImpl(
                instancePtr,
                physicalDevicePtr,
                devicePtr,
                queuePtr,
                graphicsQueueIndex,
                instanceProcAddr,
                deviceProcAddr,
                apiVersion,
                memoryAllocator
            )
            if (ptr == NullPointer) throw RenderException("Can't create Vulkan DirectContext")
            return DirectContext(ptr)
        }

        init {
            staticLoad()
        }
    }

    fun flush(): DirectContext {
        Stats.onNativeCall()
        DirectContext_nFlushDefault(_ptr)
        return this
    }

    fun flush(surface: Surface): DirectContext {
        Stats.onNativeCall()
        DirectContext_nFlush(_ptr, surface._ptr)
        return this
    }

    fun resetAll(): DirectContext {
        Stats.onNativeCall()
        _nReset(_ptr, -1)
        return this
    }

    fun resetGLAll(): DirectContext {
        Stats.onNativeCall()
        _nReset(_ptr, 0xffff)
        return this
    }

    fun resetGL(vararg states: GLBackendState): DirectContext {
        Stats.onNativeCall()
        var flags = 0
        for (state in states) flags = flags or state._bit
        _nReset(_ptr, flags)
        return this
    }

    /**
     *
     * Submit outstanding work to the gpu from all previously un-submitted flushes.
     *
     * If the syncCpu flag is true this function will return once the gpu has finished with all submitted work.
     *
     * For more information refer to skia GrDirectContext::submit(bool syncCpu) method.
     *
     * @param syncCpu flag to sync cpu and gpu work submission
     */
    fun submit(syncCpu: Boolean) {
        Stats.onNativeCall()
        _nSubmit(_ptr, syncCpu)
    }

    fun flushAndSubmit(surface: Surface, syncCpu: Boolean = false) {
        try {
            Stats.onNativeCall()
            _nFlushAndSubmit(_ptr, surface._ptr, syncCpu)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /**
     *
     * Abandons all GPU resources and assumes the underlying backend 3D API context is no longer
     * usable. Call this if you have lost the associated GPU context, and thus internal texture,
     * buffer, etc. references/IDs are now invalid. Calling this ensures that the destructors of the
     * context and any of its created resource objects will not make backend 3D API calls. Content
     * rendered but not previously flushed may be lost. After this function is called all subsequent
     * calls on the context will fail or be no-ops.
     *
     *
     * The typical use case for this function is that the underlying 3D context was lost and further
     * API calls may crash.
     *
     *
     * For Vulkan, even if the device becomes lost, the VkQueue, VkDevice, or VkInstance used to
     * create the context must be kept alive even after abandoning the context. Those objects must
     * live for the lifetime of the context object itself. The reason for this is so that
     * we can continue to delete any outstanding GrBackendTextures/RenderTargets which must be
     * cleaned up even in a device lost state.
     */
    fun abandon() {
        try {
            Stats.onNativeCall()
            _nAbandon(_ptr, 0)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /**
     * GPU resource cache limit. If the cache currently exceeds this limit,
     * it will be purged (LRU) to keep the cache within the limit.
     */
    var resourceCacheLimit: Long
        get() {
            try {
                Stats.onNativeCall()
                return DirectContext_nGetResourceCacheLimit(_ptr)
            } finally {
                reachabilityBarrier(this)
            }
        }
        set(value) {
            try {
                Stats.onNativeCall()
                DirectContext_nSetResourceCacheLimit(_ptr, value)
            } finally {
                reachabilityBarrier(this)
            }
        }

    /**
     * Frees the GPU resources no work has used for [msNotUsed] milliseconds, and handles finished GPU work. With
     * [scratchResourcesOnly], only scratch resources (layers, offscreens Skia made for itself) go; resources an image
     * or a path keyed in the cache stay.
     *
     * For more information refer to skia GrDirectContext::performDeferredCleanup.
     */
    fun performDeferredCleanup(msNotUsed: Long, scratchResourcesOnly: Boolean = false) {
        require(msNotUsed >= 0L) { "msNotUsed is negative" }
        try {
            Stats.onNativeCall()
            DirectContext_nPerformDeferredCleanup(_ptr, msNotUsed, scratchResourcesOnly)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /** Frees every GPU resource the context holds that the GPU isn't still reading (GrDirectContext::freeGpuResources). */
    fun freeGpuResources() {
        try {
            Stats.onNativeCall()
            DirectContext_nFreeGpuResources(_ptr)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /**
     * Frees the cache's unlocked resources (those no object holds), or only its scratch ones with
     * [scratchResourcesOnly] (GrDirectContext::purgeUnlockedResources).
     */
    fun purgeUnlockedResources(scratchResourcesOnly: Boolean = false) {
        try {
            Stats.onNativeCall()
            DirectContext_nPurgeUnlockedResources(_ptr, scratchResourcesOnly)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /**
     * Frees at least [bytesToPurge] bytes of unlocked resources where there are that many, least recently used
     * first, scratch ones first with [preferScratchResources] (GrDirectContext::purgeUnlockedResources). Skia
     * subtracts [bytesToPurge] from the cache's current bytes, so a figure above [resourceCacheUsageBytes] frees
     * nothing: pass at most that.
     */
    fun purgeUnlockedResources(bytesToPurge: Long, preferScratchResources: Boolean) {
        require(bytesToPurge >= 0L) { "bytesToPurge is negative" }
        try {
            Stats.onNativeCall()
            DirectContext_nPurgeUnlockedResourcesBytes(_ptr, bytesToPurge, preferScratchResources)
        } finally {
            reachabilityBarrier(this)
        }
    }

    /** Bytes of GPU memory the resource cache holds, budgeted or not (GrDirectContext::getResourceCacheUsage). */
    val resourceCacheUsageBytes: Long
        get() = resourceCacheUsage(RESOURCE_CACHE_BYTES)

    /** Resources the resource cache holds (GrDirectContext::getResourceCacheUsage). */
    val resourceCacheUsageCount: Int
        get() = resourceCacheUsage(RESOURCE_CACHE_COUNT).toInt()

    /** Bytes of the resource cache's resources that could be purged now (GrDirectContext::getResourceCachePurgeableBytes). */
    val resourceCachePurgeableBytes: Long
        get() = resourceCacheUsage(RESOURCE_CACHE_PURGEABLE_BYTES)

    private fun resourceCacheUsage(which: Int): Long =
        try {
            Stats.onNativeCall()
            DirectContext_nGetResourceCacheUsage(_ptr, which)
        } finally {
            reachabilityBarrier(this)
        }
}

private const val RESOURCE_CACHE_BYTES = 0
private const val RESOURCE_CACHE_COUNT = 1
private const val RESOURCE_CACHE_PURGEABLE_BYTES = 2

fun <R> DirectContext.useContext(block: (ctx: DirectContext) -> R): R = use {
    block(this).also { abandon() }
}

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nFlush")
// Needs ForceNativeThreadState: sends queued drawing work to the GPU through Metal.
// Native call path: GrDirectContext::flush -> GrDrawingManager::flushSurfaces -> GrMtlGpu::onSubmitToGpu
@ForceNativeThreadState
private external fun DirectContext_nFlush(ptr: NativePointer, surfacePtr: NativePointer)

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nFlushDefault")
// Needs ForceNativeThreadState: sends queued drawing work to the GPU through Metal.
// Native call path: GrDirectContext::flush -> GrDrawingManager::flushSurfaces -> GrMtlGpu::onSubmitToGpu
@ForceNativeThreadState
private external fun DirectContext_nFlushDefault(ptr: NativePointer)

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nGetResourceCacheLimit")
// Needs ForceNativeThreadState: in debug builds, can wait for another thread that is using the same GPU context.
// Native call path: GrDirectContext::getResourceCacheLimit -> SingleOwner::enter -> SkMutex::acquire ->
//   SkSemaphore::wait
@ForceNativeThreadState
private external fun DirectContext_nGetResourceCacheLimit(ptr: NativePointer): Long

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nSetResourceCacheLimit")
// Needs ForceNativeThreadState: can free cached GPU resources, such as Metal textures, to fit the new limit.
// Native call path: GrDirectContext::setResourceCacheLimit -> GrResourceCache::setLimit -> purgeAsNeeded
@ForceNativeThreadState
private external fun DirectContext_nSetResourceCacheLimit(ptr: NativePointer, maxResourceBytes: Long)

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nPerformDeferredCleanup")
// Needs ForceNativeThreadState: checks finished GPU work and frees Metal resources through the resource cache.
// Native call path: GrDirectContext::performDeferredCleanup -> GrResourceCache::purgeResourcesNotUsedSince
@ForceNativeThreadState
private external fun DirectContext_nPerformDeferredCleanup(ptr: NativePointer, msNotUsed: Long, scratchResourcesOnly: Boolean)

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nFreeGpuResources")
// Needs ForceNativeThreadState: flushes and frees Metal resources through the resource cache.
// Native call path: GrDirectContext::freeGpuResources -> GrResourceCache::purgeUnlockedResources
@ForceNativeThreadState
private external fun DirectContext_nFreeGpuResources(ptr: NativePointer)

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nPurgeUnlockedResources")
// Needs ForceNativeThreadState: frees Metal resources through the resource cache.
// Native call path: GrDirectContext::purgeUnlockedResources -> GrResourceCache::purgeUnlockedResources
@ForceNativeThreadState
private external fun DirectContext_nPurgeUnlockedResources(ptr: NativePointer, scratchResourcesOnly: Boolean)

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nPurgeUnlockedResourcesBytes")
// Needs ForceNativeThreadState: frees Metal resources through the resource cache.
// Native call path: GrDirectContext::purgeUnlockedResources -> GrResourceCache::purgeUnlockedResources
@ForceNativeThreadState
private external fun DirectContext_nPurgeUnlockedResourcesBytes(ptr: NativePointer, bytesToPurge: Long, preferScratchResources: Boolean)

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nGetResourceCacheUsage")
// Needs ForceNativeThreadState: in debug builds, can wait for another thread that is using the same GPU context.
// Native call path: GrDirectContext::getResourceCacheUsage -> SingleOwner::enter -> SkMutex::acquire
@ForceNativeThreadState
private external fun DirectContext_nGetResourceCacheUsage(ptr: NativePointer, which: Int): Long

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nMakeGL")
// Needs ForceNativeThreadState: creates a GPU context and queries OpenGL capabilities, which makes GL calls.
// Native call path: GrDirectContexts::MakeGL -> GrGLGpu::Make
@ForceNativeThreadState
private external fun _nMakeGL(): NativePointer

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nMakeMetal")
// Needs ForceNativeThreadState: creates a GPU context and queries the Metal device, which calls Metal.
// Native call path: GrDirectContexts::MakeMetal -> GrMtlTrampoline::MakeGpu -> GrMtlGpu::Make
@ForceNativeThreadState
private external fun _nMakeMetal(devicePtr: NativePointer, queuePtr: NativePointer): NativePointer

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nMakeMetalWithShaderCache")
// Needs ForceNativeThreadState: creates a GPU context and queries the Metal device, which calls Metal, and may make
// the cache's directory.
// Native call path: FileShaderCache::forDirectory, GrDirectContexts::MakeMetal -> GrMtlTrampoline::MakeGpu
@ForceNativeThreadState
private external fun _nMakeMetalWithShaderCache(devicePtr: NativePointer, queuePtr: NativePointer, shaderCacheDirectory: InteropPointer): NativePointer

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nMakeDirect3D")
// Needs ForceNativeThreadState: creates a Direct3D 12 GPU context, which is GPU setup work.
// Native call path: GrDirectContexts::MakeD3D -> GrD3DGpu::Make
@ForceNativeThreadState
private external fun _nMakeDirect3D(adapterPtr: NativePointer, devicePtr: NativePointer, queuePtr: NativePointer): NativePointer

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nMakeVulkan")
// Needs ForceNativeThreadState: creates a Vulkan GPU context, which is GPU setup work.
// Native call path: GrDirectContexts::MakeVulkan -> GrVkGpu::Make
@ForceNativeThreadState
private external fun _nMakeVulkan(
    instancePtr: NativePointer,
    physicalDevicePtr: NativePointer,
    devicePtr: NativePointer,
    queuePtr: NativePointer,
    graphicsQueueIndex: Int,
    instanceProcAddr: NativePointer,
    deviceProcAddr: NativePointer,
    apiVersion: Int,
    memoryAllocatorPtr: NativePointer
): NativePointer

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nSubmit")
// Needs ForceNativeThreadState: commits the Metal command buffer to the GPU, and can wait for the GPU to finish.
// Native call path: GrDirectContext::submit -> GrGpu::submitToGpu -> GrMtlGpu::onSubmitToGpu ->
//   GrMtlCommandBuffer::commit
@ForceNativeThreadState
private external fun _nSubmit(ptr: NativePointer, syncCpu: Boolean)

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nFlushAndSubmit")
// Needs ForceNativeThreadState: flushes queued drawing work and commits it through Metal, and can wait for the GPU to
// finish.
// Native call path: GrDirectContext::flushAndSubmit -> GrMtlGpu::onSubmitToGpu -> GrMtlCommandBuffer::commit
@ForceNativeThreadState
private external fun _nFlushAndSubmit(ptr: NativePointer, surfacePtr: NativePointer, syncCpu: Boolean)

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nReset")
// Needs ForceNativeThreadState: in debug builds, can wait for another thread that is using the same GPU context before
// resetting it.
// Native call path: GrDirectContext::resetContext -> SingleOwner::enter -> SkMutex::acquire -> SkSemaphore::wait
@ForceNativeThreadState
private external fun _nReset(ptr: NativePointer, flags: Int)

@ExternalSymbolName("org_jetbrains_skia_DirectContext__1nAbandon")
// Needs ForceNativeThreadState: waits for all outstanding GPU work, then releases the Metal queue and encoders.
// Native call path: GrDirectContext::abandonContext -> GrMtlGpu::disconnect
@ForceNativeThreadState
private external fun _nAbandon(ptr: NativePointer, flags: Int)

internal fun nMakeVulkanNoAllocator(
    instancePtr: NativePointer,
    physicalDevicePtr: NativePointer,
    devicePtr: NativePointer,
    queuePtr: NativePointer,
    graphicsQueueIndex: Int,
    instanceProcAddr: NativePointer,
    deviceProcAddr: NativePointer,
    apiVersion: Int
): NativePointer = _nMakeVulkan(
    instancePtr, physicalDevicePtr, devicePtr, queuePtr,
    graphicsQueueIndex, instanceProcAddr, deviceProcAddr, apiVersion,
    NullPointer
)
