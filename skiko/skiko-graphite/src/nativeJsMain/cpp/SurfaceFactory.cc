#include "common.h"

#include "include/core/SkColorSpace.h"
#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/graphite/Recorder.h"
#include "include/gpu/graphite/Surface.h"

SKIKO_EXPORT KNativePointer org_jetbrains_skia_gpu_graphite_SurfaceFactory__1nWrapBackendTexture(
        KNativePointer recorderPtr,
        KNativePointer backendTexturePtr,
        KNativePointer colorSpacePtr,
        KInteropPointer surfacePropsValues) {
    auto recorder = reinterpret_cast<skgpu::graphite::Recorder*>(recorderPtr);
    auto backendTexture = reinterpret_cast<skgpu::graphite::BackendTexture*>(backendTexturePtr);
    auto colorSpace = sk_ref_sp(reinterpret_cast<SkColorSpace*>(colorSpacePtr));
    auto surfaceProps = skija::SurfaceProps::toSkSurfaceProps(surfacePropsValues);
    return reinterpret_cast<KNativePointer>(SkSurfaces::WrapBackendTexture(
            recorder,
            *backendTexture,
            std::move(colorSpace),
            surfaceProps.get()).release());
}

SKIKO_EXPORT KNativePointer org_jetbrains_skia_gpu_graphite_SurfaceFactory__1nMakeRenderTarget(
        KNativePointer recorderPtr,
        KInt width,
        KInt height,
        KInt colorType,
        KInt alphaType,
        KNativePointer colorSpacePtr,
        KBoolean mipmapped,
        KInteropPointer surfacePropsValues) {
    auto recorder = reinterpret_cast<skgpu::graphite::Recorder*>(recorderPtr);
    SkImageInfo imageInfo = SkImageInfo::Make(width, height,
                                              static_cast<SkColorType>(colorType),
                                              static_cast<SkAlphaType>(alphaType),
                                              sk_ref_sp(reinterpret_cast<SkColorSpace*>(colorSpacePtr)));
    auto surfaceProps = skija::SurfaceProps::toSkSurfaceProps(surfacePropsValues);
    return reinterpret_cast<KNativePointer>(SkSurfaces::RenderTarget(
            recorder,
            imageInfo,
            mipmapped ? skgpu::Mipmapped::kYes : skgpu::Mipmapped::kNo,
            surfaceProps.get()).release());
}
