package org.jetbrains.skia

import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.addressOf
import kotlinx.cinterop.objcPtr
import kotlinx.cinterop.usePinned
import platform.Metal.MTLCreateSystemDefaultDevice
import platform.Metal.MTLPixelFormatBGRA8Unorm
import platform.Metal.MTLRegionMake2D
import platform.Metal.MTLTextureDescriptor
import platform.Metal.MTLTextureUsageShaderRead
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertTrue

@OptIn(ExperimentalForeignApi::class)
class BackendTextureMetalTest {

    @Test
    fun adoptsAMetalTextureAndReadsItsPixels() {
        // A host without a Metal device (a VM without a GPU) has nothing to wrap.
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val width = 4
        val height = 2

        val descriptor = MTLTextureDescriptor.texture2DDescriptorWithPixelFormat(
            MTLPixelFormatBGRA8Unorm, width.toULong(), height.toULong(), false
        )
        descriptor.usage = MTLTextureUsageShaderRead
        val texture = device.newTextureWithDescriptor(descriptor)!!
        // BGRA bytes of opaque red, written by the CPU before Skia sees the texture.
        val pixels = ByteArray(width * height * 4) { i ->
            when (i % 4) { 2, 3 -> 0xFF.toByte(); else -> 0 }
        }
        pixels.usePinned {
            texture.replaceRegion(
                MTLRegionMake2D(0u, 0u, width.toULong(), height.toULong()),
                0u,
                it.addressOf(0),
                (width * 4).toULong()
            )
        }

        val context = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr())
        val backendTexture = BackendTexture.makeMetal(width, height, false, texture.objcPtr())
        val image = Image.adoptTextureFrom(context, backendTexture, SurfaceOrigin.TOP_LEFT, ColorType.BGRA_8888)
        assertEquals(width, image.width)
        assertEquals(height, image.height)

        val bitmap = Bitmap()
        bitmap.allocPixels(ImageInfo.makeN32Premul(width, height))
        assertTrue(image.readPixels(context, bitmap))
        assertEquals(Color.RED, bitmap.getColor(width - 1, height - 1))

        image.close()
        backendTexture.close()
        context.close()
    }
}
