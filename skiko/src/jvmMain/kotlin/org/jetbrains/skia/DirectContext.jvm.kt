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
 * Creates [DirectContext] using EGL (Embedded-System Graphics Library).
 * This is designed for use with EGL-based implementations including ANGLE.
 *
 * There must be a current EGL context set (i.e., by calling `eglMakeCurrent` before this),
 * otherwise this function will fail.
 * 
 * This method uses Skia's GrGLMakeEGLInterface() which internally resolves
 * OpenGL/OpenGL ES function pointers via eglGetProcAddress.
 * 
 * @return A new DirectContext configured for EGL
 * @throws RenderException if the context cannot be created
 */
fun DirectContext.Companion.makeEGL(): DirectContext {
    Stats.onNativeCall()
    val ptr = _nMakeEGL()
    if (ptr == NullPointer) throw RenderException("Can't create EGL DirectContext")
    return DirectContext(ptr)
}

private external fun _nMakeGLWithInterface(interfacePtr: NativePointer): NativePointer
private external fun _nMakeEGL(): NativePointer
