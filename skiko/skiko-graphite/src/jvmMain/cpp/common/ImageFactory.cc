#include <jni.h>

#include "include/core/SkColorSpace.h"
#include "include/core/SkImage.h"
#include "include/gpu/graphite/BackendTexture.h"
#include "include/gpu/graphite/Image.h"
#include "include/gpu/graphite/Recorder.h"

extern "C" JNIEXPORT jlong JNICALL
Java_org_jetbrains_skia_gpu_graphite_ImageFactoryKt__1nWrapTexture(
        JNIEnv*,
        jclass,
        jlong recorderPtr,
        jlong backendTexturePtr,
        jint alphaType,
        jlong colorSpacePtr,
        jint origin) {
    auto recorder = reinterpret_cast<skgpu::graphite::Recorder*>(static_cast<uintptr_t>(recorderPtr));
    auto backendTexture = reinterpret_cast<skgpu::graphite::BackendTexture*>(
            static_cast<uintptr_t>(backendTexturePtr));
    return reinterpret_cast<jlong>(SkImages::WrapTexture(
            recorder,
            *backendTexture,
            static_cast<SkAlphaType>(alphaType),
            sk_ref_sp(reinterpret_cast<SkColorSpace*>(static_cast<uintptr_t>(colorSpacePtr))),
            origin == 0 ? skgpu::Origin::kTopLeft : skgpu::Origin::kBottomLeft).release());
}
