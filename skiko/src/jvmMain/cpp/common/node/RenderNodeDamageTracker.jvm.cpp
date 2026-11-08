#include <jni.h>
#include "../interop.hh"
#include "node/RenderNode.h"
#include "node/RenderNodeDamage.h"

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nMake
  (JNIEnv *env, jclass jclass) {
    auto instance = sk_make_sp<skiko::node::RenderNodeDamageTracker>();
    return reinterpret_cast<jlong>(instance.release());
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nBeginFrame
  (JNIEnv *env, jclass jclass, jlong ptr, jint width, jint height) {
    reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr)->beginFrame(width, height);
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nAddNode
  (JNIEnv *env, jclass jclass, jlong ptr, jlong nodePtr, jfloatArray matrixArr) {
    auto instance = reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr);
    auto node = reinterpret_cast<skiko::node::RenderNode *>(nodePtr);
    std::unique_ptr<SkMatrix> matrix = skMatrix(env, matrixArr);
    instance->addNode(node, matrix ? *matrix : SkMatrix::I());
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nInvalidate
  (JNIEnv *env, jclass jclass, jlong ptr) {
    reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr)->invalidate();
}

extern "C" JNIEXPORT jint JNICALL Java_org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nEndFrame
  (JNIEnv *env, jclass jclass, jlong ptr) {
    auto instance = reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr);
    instance->endFrame();
    return instance->isFull() ? -1 : static_cast<jint>(instance->getRects().size());
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nGetRects
  (JNIEnv *env, jclass jclass, jlong ptr, jintArray resultArray) {
    auto instance = reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr);
    const auto& rects = instance->getRects();
    std::vector<jint> values;
    values.reserve(rects.size() * 4);
    for (const SkIRect& rect : rects) {
        values.push_back(rect.fLeft);
        values.push_back(rect.fTop);
        values.push_back(rect.fRight);
        values.push_back(rect.fBottom);
    }
    env->SetIntArrayRegion(resultArray, 0, static_cast<jsize>(values.size()), values.data());
}

extern "C" JNIEXPORT jint JNICALL Java_org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nGetWalked
  (JNIEnv *env, jclass jclass, jlong ptr, jboolean skipped) {
    auto instance = reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr);
    return skipped ? instance->skippedNodes() : instance->walkedNodes();
}
