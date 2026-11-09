#include "common.h"
#include "node/RenderNodeRasterCache.h"

using skiko::node::RasterCache;

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nBeginFrame() {
    RasterCache::beginFrame();
}

SKIKO_EXPORT KBoolean org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nSettleRequested() {
    return RasterCache::settleRequested();
}

SKIKO_EXPORT KLong org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nGetBytes() {
    return static_cast<KLong>(RasterCache::bytes());
}

SKIKO_EXPORT KInt org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nGetCount() {
    return static_cast<KInt>(RasterCache::count());
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nPurge(KLong msNotUsed) {
    RasterCache::purge(msNotUsed);
}

SKIKO_EXPORT void org_jetbrains_skiko_node_RenderNodeCachesKt_RenderNodeCaches_1nSetBypass(KBoolean bypass) {
    RasterCache::setBypass(bypass);
}
