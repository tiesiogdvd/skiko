#include <cmath>
#include "common.h"
#include "node/RenderNode.h"
#include "node/RenderNodeAnimator.h"

using skiko::node::AlphaCompositing;
using skiko::node::AnimatedProperty;
using skiko::node::AnimationCurve;
using skiko::node::RenderNode;
using skiko::node::RenderNodeAnimationClock;

SKIKO_EXPORT KFloat org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationCurve_1nValue
  (KFloat* data, KInt size, KLong playTimeNanos, KFloat initial, KFloat target, KFloat velocity, KBoolean wantVelocity) {
    AnimationCurve curve;
    if (!AnimationCurve::decode(data, size, &curve)) return NAN;
    return wantVelocity ? curve.velocity(playTimeNanos, initial, target, velocity)
                        : curve.value(playTimeNanos, initial, target, velocity);
}

SKIKO_EXPORT KLong org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationCurve_1nDurationNanos
  (KFloat* data, KInt size, KFloat initial, KFloat target, KFloat velocity) {
    AnimationCurve curve;
    if (!AnimationCurve::decode(data, size, &curve)) return -1;
    return curve.durationNanos(initial, target, velocity);
}

SKIKO_EXPORT KNativePointer org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nMake() {
    auto instance = sk_make_sp<RenderNodeAnimationClock>();
    return reinterpret_cast<KNativePointer>(instance.release());
}

SKIKO_EXPORT KLong org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nStart
  (KNativePointer ptr, KNativePointer nodePtr, KInt property, KFloat* curveData, KInt curveSize,
   KFloat from, KFloat target, KFloat velocity, KLong startTimeNanos, KFloat durationScale, KInt alphaCompositing) {
    AnimationCurve curve;
    if (!AnimationCurve::decode(curveData, curveSize, &curve)) return 0;
    return reinterpret_cast<RenderNodeAnimationClock*>(ptr)->start(
        reinterpret_cast<RenderNode*>(nodePtr), static_cast<AnimatedProperty>(property), curve, from, target, velocity,
        startTimeNanos, durationScale, static_cast<AlphaCompositing>(alphaCompositing));
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nCancel
  (KNativePointer ptr, KNativePointer nodePtr, KInt property) {
    auto clock = reinterpret_cast<RenderNodeAnimationClock*>(ptr);
    auto node = reinterpret_cast<RenderNode*>(nodePtr);
    if (property < 0) clock->cancelAll(node);
    else clock->cancel(node, static_cast<AnimatedProperty>(property));
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nSnap
  (KNativePointer ptr, KNativePointer nodePtr, KInt property, KFloat value, KInt alphaCompositing) {
    reinterpret_cast<RenderNodeAnimationClock*>(ptr)->snap(
        reinterpret_cast<RenderNode*>(nodePtr), static_cast<AnimatedProperty>(property), value,
        static_cast<AlphaCompositing>(alphaCompositing));
}

SKIKO_EXPORT KBoolean org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nTick
  (KNativePointer ptr, KLong frameTimeNanos) {
    return reinterpret_cast<RenderNodeAnimationClock*>(ptr)->tick(frameTimeNanos);
}

SKIKO_EXPORT KLong org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nQuery
  (KNativePointer ptr, KInt what, KLong id) {
    auto clock = reinterpret_cast<RenderNodeAnimationClock*>(ptr);
    switch (what) {
        case 0: return clock->isActive() ? 1 : 0;
        case 1: return clock->endedCount();
        default: return clock->isRunning(id) ? 1 : 0;
    }
}

SKIKO_EXPORT KFloat org_jetbrains_skiko_node_RenderNodeAnimationClockKt_RenderNodeAnimationClock_1nNodeQuery
  (KNativePointer ptr, KNativePointer nodePtr, KInt property, KInt what, KLong time) {
    auto clock = reinterpret_cast<RenderNodeAnimationClock*>(ptr);
    auto node = reinterpret_cast<RenderNode*>(nodePtr);
    auto p = static_cast<AnimatedProperty>(property);
    switch (what) {
        case 0: return clock->value(node, p);
        case 1: return clock->velocity(node, p);
        default: return clock->valueAt(node, p, time);
    }
}
