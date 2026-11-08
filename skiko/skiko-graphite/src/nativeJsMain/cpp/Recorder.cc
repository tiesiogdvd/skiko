#include <chrono>
#include <optional>

#include "common.h"

#include "include/gpu/graphite/Recorder.h"

static void deleteRecorder(skgpu::graphite::Recorder* recorder) {
    delete recorder;
}

SKIKO_EXPORT KNativePointer org_jetbrains_skia_gpu_graphite_Recorder__1nGetFinalizer() {
    return reinterpret_cast<KNativePointer>(&deleteRecorder);
}

SKIKO_EXPORT KNativePointer org_jetbrains_skia_gpu_graphite_Recorder__1nSnap(
        KNativePointer recorderPtr) {
    auto recorder = reinterpret_cast<skgpu::graphite::Recorder*>(recorderPtr);
    return reinterpret_cast<KNativePointer>(recorder->snap().release());
}

SKIKO_EXPORT void org_jetbrains_skia_gpu_graphite_Recorder__1nPerformDeferredCleanup(
        KNativePointer recorderPtr, KLong msNotUsed, KLong maxPurgingMicros) {
    auto recorder = reinterpret_cast<skgpu::graphite::Recorder*>(recorderPtr);
    std::optional<std::chrono::microseconds> maxDuration;
    if (maxPurgingMicros >= 0) maxDuration = std::chrono::microseconds(maxPurgingMicros);
    recorder->performDeferredCleanup(std::chrono::milliseconds(msNotUsed), maxDuration);
}

SKIKO_EXPORT void org_jetbrains_skia_gpu_graphite_Recorder__1nFreeGpuResources(KNativePointer recorderPtr) {
    reinterpret_cast<skgpu::graphite::Recorder*>(recorderPtr)->freeGpuResources();
}

SKIKO_EXPORT KLong org_jetbrains_skia_gpu_graphite_Recorder__1nGetBudgetBytes(KNativePointer recorderPtr, KInt which) {
    auto recorder = reinterpret_cast<skgpu::graphite::Recorder*>(recorderPtr);
    switch (which) {
        case 0: return static_cast<KLong>(recorder->currentBudgetedBytes());
        case 1: return static_cast<KLong>(recorder->currentPurgeableBytes());
        default: return static_cast<KLong>(recorder->maxBudgetedBytes());
    }
}

SKIKO_EXPORT void org_jetbrains_skia_gpu_graphite_Recorder__1nSetMaxBudgetedBytes(
        KNativePointer recorderPtr, KLong bytes) {
    reinterpret_cast<skgpu::graphite::Recorder*>(recorderPtr)->setMaxBudgetedBytes(static_cast<size_t>(bytes));
}
