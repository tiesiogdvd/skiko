package org.jetbrains.skia.gpu.graphite

import org.jetbrains.skia.ColorAlphaType
import org.jetbrains.skia.ColorSpace
import org.jetbrains.skia.ExternalSymbolName
import org.jetbrains.skia.ForceNativeThreadState
import org.jetbrains.skia.Image
import org.jetbrains.skia.SurfaceOrigin
import org.jetbrains.skia.impl.Native.Companion.NullPointer
import org.jetbrains.skia.impl.NativePointer
import org.jetbrains.skia.impl.Stats
import org.jetbrains.skia.impl.getPtr
import org.jetbrains.skia.impl.reachabilityBarrier
import org.jetbrains.skiko.ExperimentalSkikoApi
import org.jetbrains.skiko.InternalSkikoApi

/**
 * Wraps [backendTexture] (a texture the caller made and keeps alive while the image is drawn) as an image that
 * [recorder]'s canvases can draw, without a copy. The color type follows the texture's format.
 *
 * @param alphaType how the texture's alpha is stored.
 * @param colorSpace the texture's color space, or `null` for none.
 * @param origin which corner of the texture is its first row.
 * @return the image, or `null` where Graphite can't sample the texture's format.
 */
@ExperimentalSkikoApi
@OptIn(InternalSkikoApi::class)
fun Image.Companion.wrapTexture(
    recorder: Recorder,
    backendTexture: BackendTexture,
    alphaType: ColorAlphaType,
    colorSpace: ColorSpace?,
    origin: SurfaceOrigin = SurfaceOrigin.TOP_LEFT,
): Image? {
    return try {
        Stats.onNativeCall()
        val ptr = _nWrapTexture(
            recorder.nativePtr,
            backendTexture.nativePtr,
            alphaType.ordinal,
            getPtr(colorSpace),
            origin.ordinal,
        )
        if (ptr == NullPointer) null else Image.adoptNativePointer(ptr)
    } finally {
        reachabilityBarrier(recorder)
        reachabilityBarrier(backendTexture)
        reachabilityBarrier(colorSpace)
    }
}

@ExternalSymbolName("org_jetbrains_skia_gpu_graphite_ImageFactory__1nWrapTexture")
// Needs ForceNativeThreadState: wraps a Metal texture through the recorder's resource provider.
// Native call path: SkImages::WrapTexture -> ResourceProvider::createWrappedTexture
@ForceNativeThreadState
private external fun _nWrapTexture(
    recorderPtr: NativePointer,
    backendTexturePtr: NativePointer,
    alphaType: Int,
    colorSpacePtr: NativePointer,
    origin: Int,
): NativePointer
