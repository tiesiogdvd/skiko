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

#ifdef SK_DIRECT3D
#include "gpu/ganesh/d3d/GrD3DTypes.h"
#include "gpu/ganesh/d3d/GrD3DBackendSurface.h"
#endif

SKIKO_EXPORT KNativePointer BackendTexture_nMakeDirect3D
  (KInt width, KInt height, KNativePointer resourcePtr, KInt format, KInt sampleCnt, KInt levelCnt) {
#ifdef SK_DIRECT3D
    GrD3DTextureResourceInfo texResInfo = {};
    ID3D12Resource* resource = reinterpret_cast<ID3D12Resource*>(resourcePtr);
    texResInfo.fResource.retain(resource);
    texResInfo.fResourceState = D3D12_RESOURCE_STATE_COMMON;
    texResInfo.fFormat = static_cast<DXGI_FORMAT>(format);
    texResInfo.fSampleCount = static_cast<uint32_t>(sampleCnt);
    texResInfo.fLevelCount = static_cast<uint32_t>(levelCnt);
    GrBackendTexture* instance = new GrBackendTexture(
        GrBackendTextures::MakeD3D(width, height, texResInfo)
    );
    return instance;
#else
    return 0;
#endif
}
