#include <iostream>
#include <stdint.h>
#include "common.h"
#include "gpu/ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/SkImageGanesh.h"
#include "include/gpu/ganesh/gl/GrGLTypes.h"
#include "gpu/ganesh/GrBackendSurface.h"

static void deleteBackendTexture(GrBackendTexture* rt) {
    delete rt;
}
SKIKO_EXPORT KNativePointer org_jetbrains_skia_BackendTexture__1nGetFinalizer() {
    return reinterpret_cast<KNativePointer>(&deleteBackendTexture);
}

SKIKO_EXPORT KNativePointer org_jetbrains_skia_BackendTexture__1nMakeGL
  (KInt width, KInt height, KBoolean isMipmapped, KInt textureId, KInt target, KInt format) {
    GrGLTextureInfo textureInfo;
    textureInfo.fID = static_cast<GrGLuint>(textureId);
    textureInfo.fTarget = static_cast<GrGLenum>(target);
    textureInfo.fFormat = static_cast<GrGLenum>(format);

    GrBackendTexture obj = GrBackendTextures::MakeGL(
        width,
        height,
        isMipmapped ? skgpu::Mipmapped::kYes : skgpu::Mipmapped::kNo,
        textureInfo
    );

    GrBackendTexture* instance = new GrBackendTexture(obj);
    return instance;
}

SKIKO_EXPORT void org_jetbrains_skia_BackendTexture__1nGLTextureParametersModified
  (KNativePointer backendTexturePtr) {
    GrBackendTexture* backendTexture = reinterpret_cast<GrBackendTexture*>(backendTexturePtr);
    GrBackendTextures::GLTextureParametersModified(backendTexture);
}

#ifdef SK_METAL
#include "gpu/ganesh/mtl/GrMtlBackendSurface.h"
#include "gpu/ganesh/mtl/GrMtlTypes.h"
#endif

SKIKO_EXPORT KNativePointer BackendTexture_nMakeMetal
  (KInt width, KInt height, KBoolean isMipmapped, KNativePointer texturePtr) {
#ifdef SK_METAL
    GrMTLHandle texture = reinterpret_cast<GrMTLHandle>(texturePtr);
    GrMtlTextureInfo textureInfo;
    textureInfo.fTexture.retain(texture);
    GrBackendTexture obj = GrBackendTextures::MakeMtl(
        width,
        height,
        isMipmapped ? skgpu::Mipmapped::kYes : skgpu::Mipmapped::kNo,
        textureInfo
    );
    GrBackendTexture* instance = new GrBackendTexture(obj);
    return instance;
#else
    return 0;
#endif
}