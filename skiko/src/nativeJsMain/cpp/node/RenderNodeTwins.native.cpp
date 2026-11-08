#include "common.h"
#include "SkCanvas.h"
#include "node/RenderNodeTwins.h"

SKIKO_EXPORT KNativePointer org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nMake() {
    auto instance = sk_make_sp<skiko::node::RenderNodeTwins>();
    return reinterpret_cast<KNativePointer>(instance.release());
}

SKIKO_EXPORT KNativePointer org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nBeginRecording
  (KNativePointer ptr, KFloat width, KFloat height) {
    return reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr)->beginRecording(width, height);
}

SKIKO_EXPORT KNativePointer org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nFinishRecording
  (KNativePointer ptr) {
    return reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr)->finishRecording().release();
}

SKIKO_EXPORT KNativePointer org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nSync
  (KNativePointer ptr, KNativePointer framePtr) {
    auto frame = reinterpret_cast<SkDrawable *>(framePtr);
    return reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr)->sync(frame).release();
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nReleasePending
  (KNativePointer ptr) {
    reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr)->releasePending();
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nClose
  (KNativePointer ptr) {
    reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr)->close();
}

SKIKO_EXPORT KInt org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeTwins_1nGetCount
  (KNativePointer ptr, KBoolean recorded) {
    auto instance = reinterpret_cast<skiko::node::RenderNodeTwins *>(ptr);
    return recorded ? instance->recordedNodes() : instance->visitedNodes();
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeTwinsKt_RenderNodeFrame_1nDraw
  (KNativePointer ptr, KNativePointer canvasPtr) {
    reinterpret_cast<SkCanvas *>(canvasPtr)->drawDrawable(reinterpret_cast<SkDrawable *>(ptr));
}
