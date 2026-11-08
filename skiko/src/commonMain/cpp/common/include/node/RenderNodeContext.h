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
    // on for every context once the process made a tracker. Applies to recordings made after.
    bool shouldTrackDamage() const { return this->trackDamage || anyDamageTracker; }
    void setTrackDamage(bool trackDamage) { this->trackDamage = trackDamage; }
    static void onDamageTrackerMade() { anyDamageTracker = true; }

    const LightGeometry& getLightGeometry() const { return this->lightGeometry; }
    // Changes whenever the lighting does, which redraws the shadows of every node.
    uint32_t getLightingGeneration() const { return this->lightingGeneration; }
    const LightInfo& getLightInfo() const { return this->lightInfo; }
    void setLightingInfo(
        const LightGeometry& lightGeometry,
        const LightInfo& lightInfo
    );

private:
    LightGeometry lightGeometry;
    LightInfo lightInfo;
    uint32_t lightingGeneration = 0;
    bool measureDrawBounds;
    bool snapshotCache;
    bool trackDamage = false;
    static std::atomic<bool> anyDamageTracker;
};

} // namespace node
} // namespace skiko
