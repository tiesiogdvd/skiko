#include <jni.h>
#include "node/RenderNodeRasterCache.h"

using skiko::node::RasterCache;

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nBeginFrame
  (JNIEnv *env, jclass jclass) {
    RasterCache::beginFrame();
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nSettleRequested
  (JNIEnv *env, jclass jclass) {
    return RasterCache::settleRequested();
}

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nGetBytes
  (JNIEnv *env, jclass jclass) {
    return static_cast<jlong>(RasterCache::bytes());
}

extern "C" JNIEXPORT jint JNICALL Java_org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nGetCount
  (JNIEnv *env, jclass jclass) {
    return static_cast<jint>(RasterCache::count());
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nPurge
  (JNIEnv *env, jclass jclass, jlong msNotUsed) {
    RasterCache::purge(msNotUsed);
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nSetBypass
  (JNIEnv *env, jclass jclass, jboolean bypass) {
    RasterCache::setBypass(bypass);
}
