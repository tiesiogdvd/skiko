package org.jetbrains.skia

import org.jetbrains.skia.impl.*
import org.jetbrains.skia.impl.Library.Companion.staticLoad

class BackendTexture internal constructor(ptr: NativePointer) : Managed(ptr, _FinalizerHolder.PTR) {
    companion object {
        /**
         * Creates BackendTexture from GL texture.
         *
         * @param width - width of the [BackendTexture] to be created
         * @param height - height of the [BackendTexture] to be created
         * @param isMipmapped - if the passed [textureId] has a GL mipmap, this should be true, otherwise false
         * @param textureId - GL id of the texture to use
         * @param textureTarget - GL enum, must be valid texture target for 2D, e.g. GL_TEXTURE_2D
         * @param textureFormat - GL enum, must be valid color format, e.g. GL_RGBA or GL_BGRA_INTEGER
         *
         * @see glTextureParametersModified
         */
        fun makeGL(
            width: Int,
            height: Int,
            isMipmapped: Boolean,
            textureId: Int,
            textureTarget: Int,
            textureFormat: Int
        ): BackendTexture {
            Stats.onNativeCall()
            val ptr = _nMakeGL(
                width,
                height,
                isMipmapped,
                textureId,
                textureTarget,
                textureFormat
            )
            return BackendTexture(ptr)
        }

        /**
         * Creates BackendTexture from a Metal texture.
         *
         * The texture is RETAINED by the backend texture; adopting it into an
         * Image (Image.adoptTextureFrom) hands that retain to Skia, which
         * releases it when the image is freed. The caller keeps (and remains
         * responsible for) its own reference.
         *
         * Metal targets only (macOS and iOS, JVM and native).
         *
         * @param width - width of the [BackendTexture] to be created
         * @param height - height of the [BackendTexture] to be created
         * @param isMipmapped - if the passed [texturePtr] has Metal mipmaps, this should be true, otherwise false
         * @param texturePtr - pointer to an MTLTexture; must be not zero
         */
        fun makeMetal(
            width: Int,
            height: Int,
            isMipmapped: Boolean,
            texturePtr: NativePointer
        ): BackendTexture {
            Stats.onNativeCall()
            return BackendTexture(_nMakeMetal(width, height, isMipmapped, texturePtr))
        }

        /**
         * Creates BackendTexture from a Direct3D 12 resource.
         *
         * The resource is RETAINED by the backend texture; adopting it into an
         * Image (Image.adoptTextureFrom) hands that retain to Skia, which
         * releases it when the image is freed. The caller keeps (and remains
         * responsible for) its own reference.
         *
         * The resource must be in D3D12_RESOURCE_STATE_COMMON at handoff (the
         * state Skia is told to assume); a caller that transitions it later
         * must notify Skia via the GrBackendTextures::SetD3DResourceState
         * path before Skia samples again.
         *
         * Direct3D targets only (Windows JVM).
         *
         * @param width - width of the [BackendTexture] to be created
         * @param height - height of the [BackendTexture] to be created
         * @param resourcePtr - pointer to an ID3D12Resource; must be not zero
         * @param format - the DXGI_FORMAT of the resource
         * @param sampleCnt - sample count of the resource (1 for non-MSAA)
         * @param levelCnt - mip level count of the resource (1 for no mips)
         */
        fun makeDirect3D(
            width: Int,
            height: Int,
            resourcePtr: NativePointer,
            format: Int,
            sampleCnt: Int,
            levelCnt: Int
        ): BackendTexture {
            Stats.onNativeCall()
            return BackendTexture(_nMakeDirect3DTexture(width, height, resourcePtr, format, sampleCnt, levelCnt))
        }

        init {
            staticLoad()
        }
    }

    /**
     * Call this to indicate to DirectContext that the texture parameters have been modified in the GL context externally
     */
    fun glTextureParametersModified() {
        return try {
            Stats.onNativeCall()
            _nGLTextureParametersModified(getPtr(this))
        } finally {
            reachabilityBarrier(this)
        }
    }

    private object _FinalizerHolder {
        val PTR = BackendTexture_nGetFinalizer()
    }
}

@ExternalSymbolName("org_jetbrains_skia_BackendTexture__1nGetFinalizer")
// No ForceNativeThreadState: returns the function that frees the object, without calling it, so it is short and cannot
// block.
// Native call path: deleteBackendTexture
private external fun BackendTexture_nGetFinalizer(): NativePointer

@ExternalSymbolName("org_jetbrains_skia_BackendTexture__1nMakeGL")
// Needs ForceNativeThreadState: wraps a GL texture ID from the backend in a texture object.
// Native call path: GrBackendTextures::MakeGL
@ForceNativeThreadState
private external fun _nMakeGL(
    width: Int,
    height: Int,
    isMipmapped: Boolean,
    textureId: Int,
    target: Int,
    format: Int
): NativePointer

@ExternalSymbolName("org_jetbrains_skia_BackendTexture__1nGLTextureParametersModified")
// No ForceNativeThreadState: only resets cached texture parameter fields, which are plain stored values, so it is short
// and cannot block.
// Native call path: GLTextureParametersModified -> invalidate
private external fun _nGLTextureParametersModified(backendTexturePtr: NativePointer)

@ExternalSymbolName("BackendTexture_nMakeMetal")
// Needs ForceNativeThreadState: retains a Metal texture through CoreFoundation.
// Native call path: GrMtlTextureInfo::fTexture.retain -> GrBackendTextures::MakeMtl
@ForceNativeThreadState
private external fun _nMakeMetal(
    width: Int,
    height: Int,
    isMipmapped: Boolean,
    texturePtr: NativePointer
): NativePointer

@ExternalSymbolName("BackendTexture_nMakeDirect3D")
// No ForceNativeThreadState: returns 0 because the Direct3D body is compiled out on every Kotlin/Native target, so it
// is short and cannot block.
private external fun _nMakeDirect3DTexture(
    width: Int,
    height: Int,
    resourcePtr: NativePointer,
    format: Int,
    sampleCnt: Int,
    levelCnt: Int
): NativePointer
