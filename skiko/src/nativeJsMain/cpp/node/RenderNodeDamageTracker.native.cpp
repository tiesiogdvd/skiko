#include "common.h"
#include "node/RenderNode.h"
#include "node/RenderNodeDamage.h"

SKIKO_EXPORT KNativePointer org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nMake() {
    auto instance = sk_make_sp<skiko::node::RenderNodeDamageTracker>();
    return reinterpret_cast<KNativePointer>(instance.release());
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nBeginFrame
  (KNativePointer ptr, KInt width, KInt height) {
    reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr)->beginFrame(width, height);
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nAddNode
  (KNativePointer ptr, KNativePointer nodePtr, KFloat* matrixArr) {
    auto instance = reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr);
    auto node = reinterpret_cast<skiko::node::RenderNode *>(nodePtr);
    std::unique_ptr<SkMatrix> matrix = skMatrix(matrixArr);
    instance->addNode(node, matrix ? *matrix : SkMatrix::I());
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nInvalidate
  (KNativePointer ptr) {
    reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr)->invalidate();
}

SKIKO_EXPORT KInt org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nEndFrame
  (KNativePointer ptr) {
    auto instance = reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr);
    instance->endFrame();
    return instance->isFull() ? -1 : static_cast<KInt>(instance->getRects().size());
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nGetRects
  (KNativePointer ptr, KInt* result) {
    auto instance = reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr);
    int i = 0;
    for (const SkIRect& rect : instance->getRects()) {
        result[i++] = rect.fLeft;
        result[i++] = rect.fTop;
        result[i++] = rect.fRight;
        result[i++] = rect.fBottom;
    }
}

SKIKO_EXPORT KInt org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nGetWalked
  (KNativePointer ptr, KBoolean skipped) {
    auto instance = reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr);
    return skipped ? instance->skippedNodes() : instance->walkedNodes();
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeDamageTrackerKt_RenderNodeDamageTracker_1nSetMeasuring
  (KNativePointer ptr, KBoolean measuring) {
    reinterpret_cast<skiko::node::RenderNodeDamageTracker *>(ptr)->setMeasuring(measuring);
}
