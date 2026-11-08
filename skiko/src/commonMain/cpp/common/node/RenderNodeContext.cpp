#include "node/RenderNodeContext.h"

namespace skiko {
namespace node {

std::atomic<bool> RenderNodeContext::anyDamageTracker{false};

RenderNodeContext::RenderNodeContext(bool measureDrawBounds, bool snapshotCache)
    : measureDrawBounds(measureDrawBounds), snapshotCache(snapshotCache) {
}

void RenderNodeContext::setLightingInfo(
    const LightGeometry& lightGeometry,
    const LightInfo& lightInfo
) {
    if (this->lightGeometry.center != lightGeometry.center ||
        this->lightGeometry.radius != lightGeometry.radius ||
        this->lightInfo.ambientShadowAlpha != lightInfo.ambientShadowAlpha ||
        this->lightInfo.spotShadowAlpha != lightInfo.spotShadowAlpha) {
        this->lightingGeneration++;
    }
    this->lightGeometry = lightGeometry;
    this->lightInfo = lightInfo;
}

} // namespace node
} // namespace skiko
