package org.jetbrains.skia

import org.jetbrains.skia.impl.Native.Companion.NullPointer
import org.jetbrains.skia.impl.NativePointer
import org.jetbrains.skia.impl.Stats
import org.jetbrains.skiko.RenderException

/**
 * Creates OpenGL [DirectContext] using the provided interface.
 * This allows usage of different OpenGL interfaces (e.g., EGL).
 *
 * There must be a current OpenGL context set (i.e., by calling `eglMakeCurrent` before this), otherwise
 * this function will fail.
 * For more information refer to skia `GrGLMakeAssembledInterface` function.
 */
fun DirectContext.Companion.makeGLWithInterface(assembledInterface: GLAssembledInterface): DirectContext {
    if (assembledInterface._ptr == NullPointer) throw RenderException("Interface pointer must not be null")
    Stats.onNativeCall()
    val ptr = _nMakeGLWithInterface(assembledInterface._ptr)
    if (ptr == NullPointer) throw RenderException("Can't create OpenGL DirectContext with provided interface")
    return DirectContext(ptr)
}

/**
 * Creates OpenGL ES [DirectContext] using EGL's eglGetProcAddress for function loading.
 * This is specifically designed for use with ANGLE or other OpenGL ES implementations.
 *
 * There must be a current OpenGL ES context set (i.e., by calling `eglMakeCurrent` before this),
 * otherwise this function will fail.
 * 
 * This method internally uses `eglGetProcAddress` to resolve OpenGL ES function pointers,
 * making it compatible with ANGLE and other EGL-based OpenGL ES implementations.
 * 
 * @return A new DirectContext configured for OpenGL ES
 * @throws RenderException if the context cannot be created
 */
fun DirectContext.Companion.makeGLES(): DirectContext {
    Stats.onNativeCall()
    val ptr = _nMakeGLES()
    if (ptr == NullPointer) throw RenderException("Can't create OpenGL ES DirectContext")
    return DirectContext(ptr)
}

private external fun _nMakeGLWithInterface(interfacePtr: NativePointer): NativePointer
private external fun _nMakeGLES(): NativePointer
