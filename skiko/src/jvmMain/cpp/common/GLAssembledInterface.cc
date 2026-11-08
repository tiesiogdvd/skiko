#include <jni.h>

#include "ganesh/gl/GrGLAssembleInterface.h"
#include "ganesh/gl/GrGLInterface.h"

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skia_GLAssembledInterfaceKt__1nCreateFromNativePointers
  (JNIEnv* env, jclass jclass, jlong ctxPtr, jlong fPtr) {
    void* ctx = reinterpret_cast<void*>(static_cast<uintptr_t>(ctxPtr));
    GrGLGetProc f = reinterpret_cast<GrGLGetProc>(static_cast<uintptr_t>(fPtr));
#if defined(__APPLE__)
    // Flow's Skia builds Ganesh GL for GLES on macOS (SK_ASSUME_GL_ES), to run on an ANGLE
    // context; the desktop-GL interface is compiled out there.
    sk_sp<const GrGLInterface> interface = GrGLMakeAssembledGLESInterface(ctx, f);
#else
    sk_sp<const GrGLInterface> interface = GrGLMakeAssembledInterface(ctx, f);
#endif
    return reinterpret_cast<jlong>(interface.release());
}
