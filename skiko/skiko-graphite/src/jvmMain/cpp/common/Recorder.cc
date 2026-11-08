#include <chrono>
#include <jni.h>
#include <optional>

#include "include/gpu/graphite/Recorder.h"

static void deleteRecorder(skgpu::graphite::Recorder* recorder) {
    delete recorder;
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_jetbrains_skia_gpu_graphite_RecorderKt__1nGetRecorderFinalizer(JNIEnv*, jclass) {
    return static_cast<jlong>(reinterpret_cast<uintptr_t>(&deleteRecorder));
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_jetbrains_skia_gpu_graphite_RecorderKt__1nSnap(
        JNIEnv*, jclass, jlong recorderPtr) {
    auto recorder = reinterpret_cast<skgpu::graphite::Recorder*>(
            static_cast<uintptr_t>(recorderPtr));
    return reinterpret_cast<jlong>(recorder->snap().release());
}

static skgpu::graphite::Recorder* recorderFrom(jlong ptr) {
    return reinterpret_cast<skgpu::graphite::Recorder*>(static_cast<uintptr_t>(ptr));
}

extern "C" JNIEXPORT void JNICALL
Java_org_jetbrains_skia_gpu_graphite_RecorderKt__1nRecorderPerformDeferredCleanup(
        JNIEnv*, jclass, jlong recorderPtr, jlong msNotUsed, jlong maxPurgingMicros) {
    std::optional<std::chrono::microseconds> maxDuration;
    if (maxPurgingMicros >= 0) maxDuration = std::chrono::microseconds(maxPurgingMicros);
    recorderFrom(recorderPtr)->performDeferredCleanup(std::chrono::milliseconds(msNotUsed), maxDuration);
}

extern "C" JNIEXPORT void JNICALL
Java_org_jetbrains_skia_gpu_graphite_RecorderKt__1nRecorderFreeGpuResources(JNIEnv*, jclass, jlong recorderPtr) {
    recorderFrom(recorderPtr)->freeGpuResources();
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_jetbrains_skia_gpu_graphite_RecorderKt__1nRecorderGetBudgetBytes(JNIEnv*, jclass, jlong recorderPtr, jint which) {
    auto recorder = recorderFrom(recorderPtr);
    switch (which) {
        case 0: return static_cast<jlong>(recorder->currentBudgetedBytes());
        case 1: return static_cast<jlong>(recorder->currentPurgeableBytes());
        default: return static_cast<jlong>(recorder->maxBudgetedBytes());
    }
}

extern "C" JNIEXPORT void JNICALL
Java_org_jetbrains_skia_gpu_graphite_RecorderKt__1nSetMaxBudgetedBytes(
        JNIEnv*, jclass, jlong recorderPtr, jlong bytes) {
    recorderFrom(recorderPtr)->setMaxBudgetedBytes(static_cast<size_t>(bytes));
}
