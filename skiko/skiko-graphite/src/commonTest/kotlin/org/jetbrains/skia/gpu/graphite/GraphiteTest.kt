package org.jetbrains.skia.gpu.graphite

import org.jetbrains.skia.Color
import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorSpace
import org.jetbrains.skia.ColorType
import org.jetbrains.skia.ImageInfo
import org.jetbrains.skia.Paint
import org.jetbrains.skia.Surface
import org.jetbrains.skia.impl.use
import org.jetbrains.skiko.ExperimentalSkikoApi
import kotlin.test.Test
import kotlin.test.assertEquals
import kotlin.test.assertNotNull
import kotlin.test.assertTrue

@OptIn(ExperimentalSkikoApi::class)
class GraphiteTest {
    @Test
    fun contextCanRecordAndSubmit() {
        withTestGraphiteContext { context ->
            context.makeRecorder().use { recorder ->
                recorder.snap().use { recording ->
                    context.insertRecording(recording)
                    context.submit(syncCpu = true)
                }
            }
        }
    }

    @Test
    fun recorderBudgetAndCleanup() {
        withTestGraphiteContext { context ->
            context.makeRecorder(gpuBudgetInBytes = 64L shl 20).use { recorder ->
                assertEquals(64L shl 20, recorder.maxBudgetedBytes)
                recorder.maxBudgetedBytes = 32L shl 20
                assertEquals(32L shl 20, recorder.maxBudgetedBytes)
                drawOffscreen(context, recorder)
                assertTrue(recorder.currentBudgetedBytes >= 0L)
                assertTrue(recorder.currentPurgeableBytes >= 0L)
                recorder.performDeferredCleanup(msNotUsed = 0L)
                recorder.performDeferredCleanup(msNotUsed = 0L, maxPurgingMicros = 1_000L)
                recorder.freeGpuResources()
                context.performDeferredCleanup(msNotUsed = 0L)
                context.freeGpuResources()
                assertTrue(context.currentBudgetedBytes >= 0L)
                assertTrue(context.currentPurgeableBytes >= 0L)
            }
        }
    }

    @Test
    fun renderTargetSnapshotDrawsOnAnotherSurface() {
        withTestGraphiteContext { context ->
            context.makeRecorder().use { recorder ->
                drawOffscreen(context, recorder)
            }
        }
    }

    /** An offscreen Graphite surface drawn, snapshotted, and its snapshot drawn onto a second surface. */
    private fun drawOffscreen(context: GraphiteContext, recorder: Recorder) {
        val info = ImageInfo(64, 64, ColorType.RGBA_8888, ColorAlphaType.PREMUL, ColorSpace.sRGB)
        val source = assertNotNull(Surface.makeRenderTarget(recorder, info))
        val target = assertNotNull(Surface.makeRenderTarget(recorder, info))
        source.use {
            target.use {
                Paint().use { paint ->
                    paint.color = Color.RED
                    source.canvas.drawRect(org.jetbrains.skia.Rect.makeWH(64f, 64f), paint)
                }
                source.makeImageSnapshot().use { image -> target.canvas.drawImage(image, 0f, 0f) }
                recorder.snap().use { recording -> context.insertRecording(recording) }
                context.submit(syncCpu = true)
            }
        }
    }
}

@OptIn(ExperimentalSkikoApi::class)
internal expect fun withTestGraphiteContext(block: (GraphiteContext) -> Unit)
