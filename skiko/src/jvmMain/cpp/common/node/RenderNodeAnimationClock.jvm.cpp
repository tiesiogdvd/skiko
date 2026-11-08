#include <cmath>
#include <vector>
#include <jni.h>
#include "../interop.hh"
#include "node/RenderNode.h"
#include "node/RenderNodeAnimator.h"

using skiko::node::AlphaCompositing;
using skiko::node::AnimatedProperty;
using skiko::node::AnimationCurve;
using skiko::node::RenderNode;
using skiko::node::RenderNodeAnimationClock;

static bool decodeCurve(JNIEnv* env, jfloatArray data, jint size, AnimationCurve* curve) {
    if (!data || size <= 0) return false;
    std::vector<jfloat> values(static_cast<size_t>(size));
    env->GetFloatArrayRegion(data, 0, size, values.data());
    return AnimationCurve::decode(values.data(), size, curve);
}

extern "C" JNIEXPORT jfloat JNICALL Java_org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationCurve_1nValue
  (JNIEnv* env, jclass jclass, jfloatArray data, jint size, jlong playTimeNanos, jfloat initial, jfloat target, jfloat velocity, jboolean wantVelocity) {
    AnimationCurve curve;
    if (!decodeCurve(env, data, size, &curve)) return NAN;
    return wantVelocity ? curve.velocity(playTimeNanos, initial, target, velocity)
                        : curve.value(playTimeNanos, initial, target, velocity);
}

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationCurve_1nDurationNanos
  (JNIEnv* env, jclass jclass, jfloatArray data, jint size, jfloat initial, jfloat target, jfloat velocity) {
    AnimationCurve curve;
    if (!decodeCurve(env, data, size, &curve)) return -1;
    return curve.durationNanos(initial, target, velocity);
}

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nMake
  (JNIEnv* env, jclass jclass) {
    auto instance = sk_make_sp<RenderNodeAnimationClock>();
    return reinterpret_cast<jlong>(instance.release());
}

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nStart
  (JNIEnv* env, jclass jclass, jlong ptr, jlong nodePtr, jint property, jfloatArray curveData, jint curveSize,
   jfloat from, jfloat target, jfloat velocity, jlong startTimeNanos, jfloat durationScale, jint alphaCompositing) {
    AnimationCurve curve;
    if (!decodeCurve(env, curveData, curveSize, &curve)) return 0;
    return reinterpret_cast<RenderNodeAnimationClock*>(ptr)->start(
        reinterpret_cast<RenderNode*>(nodePtr), static_cast<AnimatedProperty>(property), curve, from, target, velocity,
        startTimeNanos, durationScale, static_cast<AlphaCompositing>(alphaCompositing));
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nCancel
  (JNIEnv* env, jclass jclass, jlong ptr, jlong nodePtr, jint property) {
    auto clock = reinterpret_cast<RenderNodeAnimationClock*>(ptr);
    auto node = reinterpret_cast<RenderNode*>(nodePtr);
    if (property < 0) clock->cancelAll(node);
    else clock->cancel(node, static_cast<AnimatedProperty>(property));
}

extern "C" JNIEXPORT void JNICALL Java_org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nSnap
  (JNIEnv* env, jclass jclass, jlong ptr, jlong nodePtr, jint property, jfloat value, jint alphaCompositing) {
    reinterpret_cast<RenderNodeAnimationClock*>(ptr)->snap(
        reinterpret_cast<RenderNode*>(nodePtr), static_cast<AnimatedProperty>(property), value,
        static_cast<AlphaCompositing>(alphaCompositing));
}

extern "C" JNIEXPORT jboolean JNICALL Java_org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nTick
  (JNIEnv* env, jclass jclass, jlong ptr, jlong frameTimeNanos) {
    return reinterpret_cast<RenderNodeAnimationClock*>(ptr)->tick(frameTimeNanos);
}

extern "C" JNIEXPORT jlong JNICALL Java_org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nQuery
  (JNIEnv* env, jclass jclass, jlong ptr, jint what, jlong id) {
    auto clock = reinterpret_cast<RenderNodeAnimationClock*>(ptr);
    switch (what) {
        case 0: return clock->isActive() ? 1 : 0;
        case 1: return clock->endedCount();
        default: return clock->isRunning(id) ? 1 : 0;
    }
}

extern "C" JNIEXPORT jfloat JNICALL Java_org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nNodeQuery
  (JNIEnv* env, jclass jclass, jlong ptr, jlong nodePtr, jint property, jint what, jlong time) {
    auto clock = reinterpret_cast<RenderNodeAnimationClock*>(ptr);
    auto node = reinterpret_cast<RenderNode*>(nodePtr);
    auto p = static_cast<AnimatedProperty>(property);
    switch (what) {
        case 0: return clock->value(node, p);
        case 1: return clock->velocity(node, p);
        default: return clock->valueAt(node, p, time);
    }
}
