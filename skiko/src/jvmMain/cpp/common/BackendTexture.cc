#include <iostream>
#include <jni.h>
#include "ganesh/GrBackendSurface.h"
#include "SkData.h"
#include "SkImage.h"
#include <ganesh/gl/GrGLBackendSurface.h>
#include "ganesh/gl/GrGLBackendSurface.h"
#include "include/gpu/ganesh/SkImageGanesh.h"
#include "ganesh/GrDirectContext.h"
#include "ganesh/gl/GrGLDirectContext.h"


static void deleteBackendTexture(GrBackendTexture* rt) {
    // std::cout << "Deleting [GrBackendTexture " << rt << "]" << std::endl;
    delete rt;
}

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skia_BackendTextureKt_BackendTexture_1nGetFinalizer
  (JNIEnv* env, jclass jclass) {
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(&deleteBackendTexture));
}

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skia_BackendTextureKt__1nMakeGL
  (JNIEnv* env, jclass jclass, jint width, jint height, jboolean isMipmapped, jint textureId, jint target, jint format) {
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
    return reinterpret_cast<jlong>(instance);
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skia_BackendTextureKt__1nGLTextureParametersModified
  (JNIEnv* env, jclass jclass, jlong backendTexturePtr) {
    GrBackendTexture* backendTexture = reinterpret_cast<GrBackendTexture*>(static_cast<uintptr_t>(backendTexturePtr));
    GrBackendTextures::GLTextureParametersModified(backendTexture);
}

#ifdef SK_METAL
#include "ganesh/mtl/GrMtlBackendSurface.h"
#include "ganesh/mtl/GrMtlTypes.h"

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skia_BackendTextureKt__1nMakeMetal
  (JNIEnv* env, jclass jclass, jint width, jint height, jboolean isMipmapped, jlong texturePtr) {
    GrMTLHandle texture = reinterpret_cast<GrMTLHandle>(static_cast<uintptr_t>(texturePtr));
    GrMtlTextureInfo textureInfo;
    textureInfo.fTexture.retain(texture);
    GrBackendTexture obj = GrBackendTextures::MakeMtl(
        width,
        height,
        isMipmapped ? skgpu::Mipmapped::kYes : skgpu::Mipmapped::kNo,
        textureInfo
    );
    GrBackendTexture* instance = new GrBackendTexture(obj);
    return reinterpret_cast<jlong>(instance);
}
#endif // SK_METAL

#ifdef SK_DIRECT3D
#include "ganesh/d3d/GrD3DTypes.h"
#include "ganesh/d3d/GrD3DBackendSurface.h"

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skia_BackendTextureKt__1nMakeDirect3DTexture
  (JNIEnv* env, jclass jclass, jint width, jint height, jlong resourcePtr, jint format, jint sampleCnt, jint levelCnt) {
    GrD3DTextureResourceInfo texResInfo = {};
    ID3D12Resource* resource = reinterpret_cast<ID3D12Resource*>(static_cast<uintptr_t>(resourcePtr));
    texResInfo.fResource.retain(resource);
    texResInfo.fResourceState = D3D12_RESOURCE_STATE_COMMON;
    texResInfo.fFormat = static_cast<DXGI_FORMAT>(format);
    texResInfo.fSampleCount = static_cast<uint32_t>(sampleCnt);
    texResInfo.fLevelCount = static_cast<uint32_t>(levelCnt);
    GrBackendTexture* instance = new GrBackendTexture(
        GrBackendTextures::MakeD3D(width, height, texResInfo)
    );
    return reinterpret_cast<jlong>(instance);
}
#endif // SK_DIRECT3D
