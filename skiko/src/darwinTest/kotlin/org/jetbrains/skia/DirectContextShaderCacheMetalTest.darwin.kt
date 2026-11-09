package org.jetbrains.skia

import kotlinx.cinterop.ExperimentalForeignApi
import kotlinx.cinterop.objcPtr
import platform.Foundation.NSFileManager
import platform.Foundation.NSTemporaryDirectory
import platform.Metal.MTLCreateSystemDefaultDevice
import kotlin.random.Random
import kotlin.test.Test
import kotlin.test.assertTrue
import kotlin.time.TimeSource

/**
 * Ganesh's persistent shader cache on Metal: a context with a cache directory stores the programs it builds there, and
 * the next context loads them. Measures an effect's first frame (a blurred layer, a blurred shape, a runtime shader new to this run)
 * with an empty directory, with the directory the first context filled, and without a cache.
 */
@OptIn(ExperimentalForeignApi::class)
class DirectContextShaderCacheMetalTest {
    @Test
    fun aWarmCacheShortensAnEffectsFirstFrame() {
        val device = MTLCreateSystemDefaultDevice() ?: return
        val queue = device.newCommandQueue() ?: return
        val directory = NSTemporaryDirectory() + "skiko-shader-cache-" + Random.nextLong().toULong().toString(16)
        // New to this run, so that neither cache (Skia's, the system's Metal one) has seen it.
        val tint = Random.nextInt(1, 1000) / 1000f
        val sksl = """
            uniform float2 size;
            half4 main(float2 p) {
                float2 uv = p / size;
                float d = length(uv - 0.5);
                return half4(half3(uv.x, uv.y * $tint, 0.5 + 0.5 * sin(d * 40.0)), 1.0);
            }
        """.trimIndent()

        fun firstFrame(context: DirectContext): Double {
            val surface = Surface.makeRenderTarget(context, false, ImageInfo.makeN32Premul(512, 512))!!
            val effect = RuntimeEffect.makeForShader(sksl)
            val builder = RuntimeShaderBuilder(effect)
            builder.uniform("size", 512f, 512f)
            val start = TimeSource.Monotonic.markNow()
            val canvas = surface.canvas
            canvas.drawRect(Rect(0f, 0f, 512f, 512f), Paint().apply { shader = builder.makeShader() })
            canvas.drawCircle(256f, 256f, 120f, Paint().apply {
                color = Color.makeARGB(180, 30, 60, 220)
                maskFilter = MaskFilter.makeBlur(FilterBlurMode.NORMAL, 6f)
            })
            canvas.saveLayer(Rect(64f, 64f, 448f, 448f), Paint().apply { imageFilter = ImageFilter.makeBlur(18f, 18f, FilterTileMode.DECAL) })
            canvas.drawRRect(RRect.makeXYWH(96f, 96f, 320f, 320f, 40f), Paint().apply { color = Color.makeARGB(200, 255, 255, 255) })
            canvas.restore()
            context.flush()
            context.submit(syncCpu = true)
            val ms = start.elapsedNow().inWholeMicroseconds / 1000.0
            builder.close(); effect.close(); surface.close()
            return ms
        }

        val cold = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr(), directory).let { context ->
            firstFrame(context).also { context.close() }
        }
        val stored = NSFileManager.defaultManager.contentsOfDirectoryAtPath(directory, null)?.size ?: 0
        val warm = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr(), directory).let { context ->
            firstFrame(context).also { context.close() }
        }
        val none = DirectContext.makeMetal(device.objcPtr(), queue.objcPtr()).let { context ->
            firstFrame(context).also { context.close() }
        }
        println("shader cache: first frame of the effects ${cold} ms with an empty cache ($stored programs stored), " +
            "${warm} ms with it warm, ${none} ms without a cache (the system's Metal cache warm by then)")
        NSFileManager.defaultManager.removeItemAtPath(directory, null)
        assertTrue(stored > 0, "the first context stored its programs")
    }
}
