package org.jetbrains.skia.gpu.graphite

import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.objcPtr
import org.jetbrains.skia.impl.use
import org.jetbrains.skiko.ExperimentalSkikoApi
import org.jetbrains.skiko.OS
import org.jetbrains.skiko.hostOs
import platform.Metal.MTLCreateSystemDefaultDevice

@OptIn(ExperimentalForeignApi::class, ExperimentalSkikoApi::class)
internal actual fun withTestGraphiteContext(block: (GraphiteContext) -> Unit) {
    if (hostOs == OS.Tvos) return

    makeTestMetalContext().use(block)
}

@OptIn(ExperimentalForeignApi::class, ExperimentalSkikoApi::class)
private fun makeTestMetalContext(): GraphiteContext {
    val device = checkNotNull(MTLCreateSystemDefaultDevice()) {
        "Metal is not supported on this system"
    }
    val queue = checkNotNull(device.newCommandQueue()) {
        "Failed to create a Metal command queue"
    }
    return GraphiteContext.makeMetal(device.objcPtr(), queue.objcPtr())
}

/** Runs [block] with a Graphite context and a 16×16 BGRA Metal texture on the same device. */
@OptIn(ExperimentalForeignApi::class, ExperimentalSkikoApi::class)
internal fun withTestMetalTexture(block: (GraphiteContext, org.jetbrains.skia.impl.NativePointer) -> Unit) {
    if (hostOs == OS.Tvos) return
    val device = checkNotNull(MTLCreateSystemDefaultDevice()) { "Metal is not supported on this system" }
    val queue = checkNotNull(device.newCommandQueue()) { "Failed to create a Metal command queue" }
    val descriptor = platform.Metal.MTLTextureDescriptor.texture2DDescriptorWithPixelFormat(
        platform.Metal.MTLPixelFormatBGRA8Unorm, 16u, 16u, false,
    )
    descriptor.usage = platform.Metal.MTLTextureUsageShaderRead or platform.Metal.MTLTextureUsageRenderTarget
    descriptor.storageMode = platform.Metal.MTLStorageModePrivate
    val texture = checkNotNull(device.newTextureWithDescriptor(descriptor)) { "Failed to create a Metal texture" }
    GraphiteContext.makeMetal(device.objcPtr(), queue.objcPtr()).use { context ->
        block(context, texture.objcPtr())
    }
}
