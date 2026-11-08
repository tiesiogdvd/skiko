#pragma once
#include <atomic>
#include <cstdint>
#include <SkRefCnt.h>
#include "Lighting.h"

namespace skiko {
namespace node {

class RenderNode;

class RenderNodeContext : public SkRefCnt {
public:
    RenderNodeContext(bool measureDrawBounds, bool snapshotCache);

    bool shouldMeasureDrawBounds() const { return this->measureDrawBounds; }
    // Whether the nodes created with this context keep an SkPicture snapshot of their
    // recorded content. A snapshot inlines the drawing of the nodes below it, so this
    // holds for the whole tree rather than for a single node.
    bool shouldUseSnapshotCache() const { return this->snapshotCache; }

    // Whether the nodes measure what their recordings draw, for a RenderNodeDamageTracker: set, or
    // on for every context while the process has a measuring tracker. Applies to recordings made after.
    bool shouldTrackDamage() const { return !this->twin && (this->trackDamage || measuringTrackers.load(std::memory_order_relaxed) > 0); }
    void setTrackDamage(bool trackDamage) { this->trackDamage = trackDamage; }
    static void onTrackerMeasuring(bool measuring) { measuringTrackers.fetch_add(measuring ? 1 : -1, std::memory_order_relaxed); }

    const LightGeometry& getLightGeometry() const { return this->lightGeometry; }
    // Changes whenever the lighting does, which redraws the shadows of every node.
    uint32_t getLightingGeneration() const { return this->lightingGeneration; }
    const LightInfo& getLightInfo() const { return this->lightInfo; }
    void setLightingInfo(
        const LightGeometry& lightGeometry,
        const LightInfo& lightInfo
    );

    // The context of this context's nodes' render twins (see RenderNodeTwins): the same options and
    // this context's lighting as of the call; its nodes never measure for damage tracking. The
    // nodes' thread.
    RenderNodeContext* twinContext();
    bool isTwin() const { return this->twin; }

private:
    LightGeometry lightGeometry;
    LightInfo lightInfo;
    uint32_t lightingGeneration = 0;
    bool measureDrawBounds;
    bool snapshotCache;
    bool trackDamage = false;
    // Whether this is another context's twin context, and this context's twin context with the
    // lighting generation it last copied.
    bool twin = false;
    sk_sp<RenderNodeContext> twinCtx;
    uint32_t twinLightingGeneration = 0;
    static std::atomic<int> measuringTrackers;
};

} // namespace node
} // namespace skiko
