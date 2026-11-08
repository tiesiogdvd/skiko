package org.jetbrains.skia

import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.objcPtr
import platform.Metal.MTLCreateSystemDefaultDevice
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertFailsWith
import kotlin.test.assertTrue

@OptIn(ExperimentalForeignApi::class)
class DirectContextCleanupMetalTest {

    private fun withContext(block: (DirectContext) -> Unit) {
        // A host without a Metal device (a VM without a GPU) has no context to clean.
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val context = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr())
        try {
            block(context)
        } finally {
            context.close()
        }
    }

    /** Draws through a layer, so that the cache keeps a scratch texture once the surface is gone. */
    private fun drawSomething(context: DirectContext) {
        val surface = Surface.makeRenderTarget(context, true, ImageInfo.makeN32Premul(256, 256))
        surface.canvas.saveLayer(Rect(0f, 0f, 200f, 200f), Paint().apply { alpha = 128 })
        surface.canvas.drawCircle(100f, 100f, 80f, Paint().apply { color = Color.RED })
        surface.canvas.restore()
        context.flush(surface)
        context.submit(syncCpu = true)
        surface.close()
    }

    @Test
    fun freeGpuResourcesEmptiesTheCache() = withContext { context ->
        drawSomething(context)
        assertTrue(context.resourceCacheUsageBytes > 0)
        assertTrue(context.resourceCacheUsageCount > 0)
        assertTrue(context.resourceCachePurgeableBytes > 0)

        context.freeGpuResources()
        assertEquals(0L, context.resourceCachePurgeableBytes)
        assertTrue(context.resourceCacheUsageBytes < 256L * 256 * 4)
    }

    @Test
    fun purgeUnlockedResourcesFreesWhatNothingHolds() = withContext { context ->
        drawSomething(context)
        val before = context.resourceCacheUsageBytes
        context.purgeUnlockedResources(scratchResourcesOnly = true)
        assertTrue(context.resourceCacheUsageBytes < before)

        drawSomething(context)
        context.purgeUnlockedResources(bytesToPurge = context.resourceCacheUsageBytes, preferScratchResources = true)
        assertEquals(0L, context.resourceCachePurgeableBytes)

        drawSomething(context)
        context.purgeUnlockedResources()
        assertEquals(0L, context.resourceCachePurgeableBytes)
    }

    @Test
    fun deferredCleanupKeepsRecentResources() = withContext { context ->
        drawSomething(context)
        val held = context.resourceCachePurgeableBytes
        assertTrue(held > 0)
        // Everything was used just now, so an hour's threshold frees nothing; zero frees all of it.
        context.performDeferredCleanup(msNotUsed = 3_600_000L)
        assertEquals(held, context.resourceCachePurgeableBytes)
        platform.posix.usleep(20_000u)
        context.performDeferredCleanup(msNotUsed = 0L)
        assertEquals(0L, context.resourceCachePurgeableBytes)

        assertFailsWith<IllegalArgumentException> { context.performDeferredCleanup(-1L) }
        assertFailsWith<IllegalArgumentException> { context.purgeUnlockedResources(-1L, false) }
    }
}
