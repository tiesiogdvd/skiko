package org.jetbrains.skia.gpu.graphite

import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorSpace
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.Image
import org.jetbrains.skia.ImageInfo
import org.jetbrains.skia.Surface
import org.jetbrains.skia.impl.use
import org.jetbrains.skiko.ExperimentalSkikoApi
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull

@OptIn(ExperimentalSkikoApi::class)
class GraphiteWrapTextureTest {
    @Test
    fun metalTextureWrapsAsAnImageARecorderDraws() {
        withTestMetalTexture { context, texture ->
            context.makeRecorder().use { recorder ->
                BackendTexture.makeMetal(16, 16, texture).use { backend ->
                    val image = assertNotNull(
                        Image.wrapTexture(recorder, backend, ColorAlphaType.PREMUL, ColorSpace.sRGB),
                    )
                    image.use {
                        assertEquals(16, image.width)
                        assertEquals(ColorType.BGRA_8888, image.colorType)
                        val info = ImageInfo(16, 16, ColorType.BGRA_8888, ColorAlphaType.PREMUL, ColorSpace.sRGB)
                        assertNotNull(Surface.makeRenderTarget(recorder, info)).use { target ->
                            target.canvas.drawImage(image, 0f, 0f)
                            recorder.snap().use { recording -> context.insertRecording(recording) }
                            context.submit(syncCpu = true)
                        }
                    }
                }
            }
        }
    }
}
