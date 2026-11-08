#include "common.h"

#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/graphite/Image.h"
#include "include/gpu/graphite/Recorder.h"

SKIKO_EXPORT KNativePointer org_jetbrains_skia_gpu_graphite_ImageFactory__1nWrapTexture(
        KNativePointer recorderPtr,
        KNativePointer backendTexturePtr,
        KInt alphaType,
        KNativePointer colorSpacePtr,
        KInt origin) {
    auto recorder = reinterpret_cast<skgpu::graphite::Recorder*>(recorderPtr);
    auto backendTexture = reinterpret_cast<skgpu::graphite::BackendTexture*>(backendTexturePtr);
    return reinterpret_cast<KNativePointer>(SkImages::WrapTexture(
            recorder,
            *backendTexture,
            static_cast<SkAlphaType>(alphaType),
            sk_ref_sp(reinterpret_cast<SkColorSpace*>(colorSpacePtr)),
            origin == 0 ? skgpu::Origin::kTopLeft : skgpu::Origin::kBottomLeft).release());
}
