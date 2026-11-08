#include "node/RenderNodeContext.h"

namespace skiko {
namespace node {

std::atomic<int> RenderNodeContext::measuringTrackers{0};

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

RenderNodeContext* RenderNodeContext::twinContext() {
    if (!this->twinCtx) {
        this->twinCtx = sk_make_sp<RenderNodeContext>(this->measureDrawBounds, this->snapshotCache);
        this->twinCtx->twin = true;
        this->twinCtx->setLightingInfo(this->lightGeometry, this->lightInfo);
        this->twinLightingGeneration = this->lightingGeneration;
    } else if (this->twinLightingGeneration != this->lightingGeneration) {
        this->twinCtx->setLightingInfo(this->lightGeometry, this->lightInfo);
        this->twinLightingGeneration = this->lightingGeneration;
    }
    return this->twinCtx.get();
}

} // namespace node
} // namespace skiko
