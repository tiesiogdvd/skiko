#include <jni.h>
#include "../interop.hh"
#include "SkCanvas.h"
#include "node/RenderNodeTwins.h"

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nMake
  (JNIEnv *env, jclass jclass) {
    auto instance = sk_make_sp<skiko::node::RenderNodeTwins>();
    return reinterpret_cast<jlong>(instance.release());
}

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nBeginRecording
  (JNIEnv *env, jclass jclass, jlong ptr, jfloat width, jfloat height) {
    return reinterpret_cast<jlong>(reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr)->beginRecording(width, height));
}

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nFinishRecording
  (JNIEnv *env, jclass jclass, jlong ptr) {
    return reinterpret_cast<jlong>(reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr)->finishRecording().release());
}

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nSync
  (JNIEnv *env, jclass jclass, jlong ptr, jlong framePtr) {
    auto frame = reinterpret_cast<SkDrawable *>(framePtr);
    return reinterpret_cast<jlong>(reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr)->sync(frame).release());
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nReleasePending
  (JNIEnv *env, jclass jclass, jlong ptr) {
    reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr)->releasePending();
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nClose
  (JNIEnv *env, jclass jclass, jlong ptr) {
    reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr)->close();
}

extern "C" JNIEXPORT jint JNICALL Java_org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nGetCount
  (JNIEnv *env, jclass jclass, jlong ptr, jboolean recorded) {
    auto instance = reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr);
    return recorded ? instance->recordedNodes() : instance->visitedNodes();
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeFrame_1nDraw
  (JNIEnv *env, jclass jclass, jlong ptr, jlong canvasPtr) {
    reinterpret_cast<SkCanvas *>(canvasPtr)->drawDrawable(reinterpret_cast<SkDrawable *>(ptr));
}
