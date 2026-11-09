#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <optional>
#include <unordered_map>
#include <vector>
#include <SkBlendMode.h>
#include <SkColorFilter.h>
#include <SkImageFilter.h>
#include <SkMaskFilter.h>
#include <SkPathEffect.h>
#include <SkNoDrawCanvas.h>
#include <SkPicture.h>
#include <SkSamplingOptions.h>
#include <SkSurface.h>
#include <SkTextBlob.h>
#include "src/core/SkCanvasPriv.h"
#include "src/core/SkDevice.h"
#include "node/RenderNode.h"
#include "node/RenderNodeContext.h"
#include "node/RenderNodeRasterCache.h"

namespace skiko {
namespace node {

// ---------------------------------------------------------------------------------------------
// Switches.
// ---------------------------------------------------------------------------------------------

static const char* env(const char* name) {
    return getenv(name);
}

static bool envIs(const char* name, const char* value) {
    const char* v = env(name);
    return v && strcmp(v, value) == 0;
}

static long envLong(const char* name, long fallback) {
    const char* v = env(name);
    if (!v || !*v) {
        return fallback;
    }
    char* end = nullptr;
    long parsed = strtol(v, &end, 10);
    return end && *end == '\0' ? parsed : fallback;
}

bool RasterCache::retainLayers() {
    static const bool enabled = !envIs("SKIKO_RENDER_NODE_LAYER_RETAIN", "0");
    return enabled;
}

// Textures that differ from the direct drawing by up to 1/255 (an image filter layer's output at
// its translucent fringe, a content texture where translucent drawing overlaps): Ganesh only by
// default, where the layer atlas already differs as much; Graphite stays bit-exact unless asked.
bool RasterCache::retainFilteredLayers(bool graphite) {
    // SKIKO_RENDER_NODE_LAYER_RETAIN: 0 none; plain: no filtered ones; 1 or all: on both.
    static const int mode = envIs("SKIKO_RENDER_NODE_LAYER_RETAIN", "0") || envIs("SKIKO_RENDER_NODE_LAYER_RETAIN", "plain") ? 0
        : envIs("SKIKO_RENDER_NODE_LAYER_RETAIN", "1") || envIs("SKIKO_RENDER_NODE_LAYER_RETAIN", "all") ? 2 : 1;
    return mode == 2 || (mode == 1 && !graphite);
}

bool RasterCache::cacheContent(bool graphite) {
    // SKIKO_RENDER_NODE_RASTER_CACHE: 0 off; 1 on both; unset or ganesh: Ganesh only.
    static const int mode = envIs("SKIKO_RENDER_NODE_RASTER_CACHE", "0") ? 0
        : envIs("SKIKO_RENDER_NODE_RASTER_CACHE", "1") ? 2 : 1;
    return mode == 2 || (mode == 1 && !graphite);
}

bool RasterCache::promote() {
    static const bool enabled = !envIs("SKIKO_RENDER_NODE_PROMOTE", "0");
    return enabled;
}

bool RasterCache::log() {
    static const bool enabled = envIs("SKIKO_RENDER_NODE_LAYER_ATLAS", "log");
    return enabled;
}

size_t RasterCache::budget() {
    static const size_t bytes = static_cast<size_t>(std::max(0L, envLong("SKIKO_RENDER_NODE_LAYER_RETAIN_MB", 96))) << 20;
    return bytes;
}

int64_t RasterCache::maxAgeMs() {
    static const int64_t ms = std::max(0L, envLong("SKIKO_RENDER_NODE_CACHE_MAX_AGE_MS", 5000));
    return ms;
}

// A node's content is cached once it costs this much to draw (see ContentAnalysisCanvas).
static int contentMinCost() {
    static const int cost = static_cast<int>(std::max(1L, envLong("SKIKO_RENDER_NODE_RASTER_CACHE_MIN_COST", 8)));
    return cost;
}

// Draws in a row with the same content before a texture is made (doubled for each time a texture
// went stale within kPayback composites), frames in a row of motion before a promotion, new
// textures a frame at most.
static constexpr int kStableDraws = 3;
static constexpr int kPayback = 4;
static constexpr int kMaxChurn = 5;
static constexpr int kPromoteAfterFrames = 2;
static constexpr int kMaxNewPerFrame = 4;
static constexpr int kMaxTextureSize = 4096;
// Frees unused textures at most this often from a frame's start.
static constexpr int64_t kSweepEveryMs = 500;

static int64_t nowMs() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------------------------
// Registry: every texture with an image, for the budget, the sweeps and the owners going away.
// ---------------------------------------------------------------------------------------------

static std::mutex registryMutex;
static std::vector<NodeTexture*> registry;
static std::atomic<size_t> registryBytes{0};
static std::atomic<int> registryCount{0};
// The Graphite recorders seen, each with the generation it was first seen under.
static std::unordered_map<const void*, uint64_t> recorderGenerations;
static uint64_t nextGeneration = 1;
// Changes whenever a recorder goes, so that a thread's cached lookup knows to look again.
static std::atomic<uint64_t> ownersEpoch{1};
// Images to release on the thread that made them (Ganesh resources belong to their context's thread).
static std::vector<std::pair<std::thread::id, sk_sp<SkImage>>> graveyard;
static std::atomic<bool> graveyardNonEmpty{false};

struct ThreadState {
    uint64_t frame = 1;
    bool hostFrames = false;
    int madeThisFrame = 0;
    int renderingDepth = 0;
    bool bypass = false;
    int inexactThisFrame = 0;
    std::vector<sk_sp<RenderNode>> unsettled;
    int64_t lastSweepMs = 0;
    // The last recorder looked up, its generation and the owners' epoch then.
    const void* recorder = nullptr;
    uint64_t recorderGeneration = 0;
    uint64_t recorderEpoch = 0;
};

static ThreadState& state() {
    static thread_local ThreadState threadState;
    return threadState;
}

static void drainGraveyard() {
    if (!graveyardNonEmpty.load(std::memory_order_relaxed)) {
        return;
    }
    std::vector<sk_sp<SkImage>> mine;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        auto here = std::this_thread::get_id();
        for (auto it = graveyard.begin(); it != graveyard.end();) {
            if (it->first == here) {
                mine.push_back(std::move(it->second));
                it = graveyard.erase(it);
            } else {
                ++it;
            }
        }
        graveyardNonEmpty.store(!graveyard.empty(), std::memory_order_relaxed);
    }
    // Released here, outside the lock.
}

static void registerLocked(NodeTexture* texture) {
    texture->slot = registry.size();
    registry.push_back(texture);
    registryBytes.fetch_add(texture->bytes, std::memory_order_relaxed);
    registryCount.fetch_add(1, std::memory_order_relaxed);
}

static void unregisterLocked(NodeTexture* texture) {
    if (texture->slot == SIZE_MAX) {
        return;
    }
    size_t slot = texture->slot;
    NodeTexture* last = registry.back();
    registry[slot] = last;
    last->slot = slot;
    registry.pop_back();
    texture->slot = SIZE_MAX;
    registryBytes.fetch_sub(texture->bytes, std::memory_order_relaxed);
    registryCount.fetch_sub(1, std::memory_order_relaxed);
}

void RasterCache::unregister(NodeTexture* texture) {
    std::lock_guard<std::mutex> lock(registryMutex);
    unregisterLocked(texture);
}

// Takes texture's image out (unregistering it), to release outside the lock or hand over.
static sk_sp<SkImage> takeImageLocked(NodeTexture* texture) {
    unregisterLocked(texture);
    texture->bytes = 0;
    return std::move(texture->image);
}

NodeTexture::~NodeTexture() {
    sk_sp<SkImage> image;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        image = takeImageLocked(this);
        if (image && !this->owner.graphite && this->thread != std::this_thread::get_id()) {
            graveyard.emplace_back(this->thread, std::move(image));
            graveyardNonEmpty.store(true, std::memory_order_relaxed);
        }
    }
}

// ---------------------------------------------------------------------------------------------
// Owners.
// ---------------------------------------------------------------------------------------------

TextureOwner RasterCache::ownerOf(SkCanvas* canvas) {
    TextureOwner owner;
    if (skgpu::graphite::Recorder* recorder = canvas->recorder()) {
        ThreadState& s = state();
        uint64_t epoch = ownersEpoch.load(std::memory_order_acquire);
        if (s.recorder != recorder || s.recorderEpoch != epoch) {
            std::lock_guard<std::mutex> lock(registryMutex);
            auto it = recorderGenerations.find(recorder);
            if (it == recorderGenerations.end()) {
                it = recorderGenerations.emplace(recorder, nextGeneration++).first;
            }
            s.recorder = recorder;
            s.recorderGeneration = it->second;
            s.recorderEpoch = epoch;
        }
        owner.object = recorder;
        owner.generation = s.recorderGeneration;
        owner.graphite = true;
    } else if (GrRecordingContext* context = canvas->recordingContext()) {
        // A Ganesh image holds its context, so the address names one context while a texture lives.
        owner.object = context;
        owner.generation = 1;
    }
    return owner;
}

void RasterCache::onGraphiteRecorderClosing(const void* recorder) {
    std::vector<sk_sp<SkImage>> released;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        recorderGenerations.erase(recorder);
        ownersEpoch.fetch_add(1, std::memory_order_acq_rel);
        // A Graphite image can be released on any thread; the node keeps an empty texture,
        // dropped when it next draws.
        for (size_t i = 0; i < registry.size();) {
            NodeTexture* texture = registry[i];
            if (texture->owner.graphite && texture->owner.object == recorder) {
                released.push_back(takeImageLocked(texture));
            } else {
                i++;
            }
        }
    }
}

void RasterCache::onGaneshContextReleasing(const void* context) {
    purge(0, context);
}

// ---------------------------------------------------------------------------------------------
// Frames.
// ---------------------------------------------------------------------------------------------

uint64_t RasterCache::frame() {
    return state().frame;
}

bool RasterCache::hostFrames() {
    return state().hostFrames;
}

static void sweepIfDue() {
    ThreadState& s = state();
    int64_t now = nowMs();
    if (now - s.lastSweepMs < kSweepEveryMs) {
        return;
    }
    s.lastSweepMs = now;
    RasterCache::purge(RasterCache::maxAgeMs());
}

void RasterCache::onTopLevelDraw() {
    ThreadState& s = state();
    drainGraveyard();
    if (!s.hostFrames && !s.bypass) {
        s.frame++;
        s.madeThisFrame = 0;
        sweepIfDue();
    }
}

void RasterCache::beginFrame() {
    ThreadState& s = state();
    drainGraveyard();
    s.hostFrames = true;
    s.frame++;
    s.madeThisFrame = 0;
    s.inexactThisFrame = 0;
    std::vector<sk_sp<RenderNode>> unsettled = std::move(s.unsettled);
    s.unsettled.clear();
    for (auto& node : unsettled) {
        node->settle();
    }
    sweepIfDue();
}

bool RasterCache::settleRequested() {
    return state().inexactThisFrame > 0;
}

void RasterCache::drewInexact(RenderNode* node) {
    ThreadState& s = state();
    s.inexactThisFrame++;
    if (s.unsettled.empty() || s.unsettled.back().get() != node) {
        s.unsettled.push_back(sk_ref_sp(node));
    }
}

bool RasterCache::rendering() {
    return state().renderingDepth > 0;
}

RasterCache::RenderingScope::RenderingScope() {
    state().renderingDepth++;
}

RasterCache::RenderingScope::~RenderingScope() {
    state().renderingDepth--;
}

bool RasterCache::bypassed() {
    return state().bypass;
}

void RasterCache::setBypass(bool bypass) {
    state().bypass = bypass;
}

// ---------------------------------------------------------------------------------------------
// Textures.
// ---------------------------------------------------------------------------------------------

bool RasterCache::mayRenderThisFrame() {
    return state().madeThisFrame < kMaxNewPerFrame;
}

void RasterCache::used(NodeTexture* texture) {
    texture->usedFrame = state().frame;
    texture->usedMs = nowMs();
    texture->hits++;
}

// Frees this thread's textures not used in this frame, least recently used first, until bytes
// more fit the budget; whether they do.
static bool makeRoom(size_t bytes) {
    size_t budget = RasterCache::budget();
    if (bytes > budget) {
        return false;
    }
    if (registryBytes.load(std::memory_order_relaxed) + bytes <= budget) {
        return true;
    }
    std::vector<sk_sp<SkImage>> released;
    bool fits;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        auto here = std::this_thread::get_id();
        uint64_t frame = state().frame;
        std::vector<NodeTexture*> candidates;
        for (NodeTexture* texture : registry) {
            if (texture->thread == here && texture->usedFrame != frame) {
                candidates.push_back(texture);
            }
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](NodeTexture* a, NodeTexture* b) { return a->usedMs < b->usedMs; });
        for (NodeTexture* texture : candidates) {
            if (registryBytes.load(std::memory_order_relaxed) + bytes <= budget) {
                break;
            }
            released.push_back(takeImageLocked(texture));
        }
        fits = registryBytes.load(std::memory_order_relaxed) + bytes <= budget;
    }
    return fits;
}

std::unique_ptr<NodeTexture> RasterCache::render(SkCanvas* target, const TextureOwner& owner, int width, int height,
                                                 const std::function<void(SkCanvas*)>& draw) {
    ThreadState& s = state();
    if (!owner || s.madeThisFrame >= kMaxNewPerFrame ||
        width <= 0 || height <= 0 || width > kMaxTextureSize || height > kMaxTextureSize) {
        return nullptr;
    }
    // A layer's color type, as SkCanvas::internalSaveLayer gives it.
    SkImageInfo targetInfo = target->imageInfo();
    SkColorType colorType = targetInfo.colorType();
    if (SkColorTypeBytesPerPixel(colorType) <= 4 &&
        colorType != kRGBA_8888_SkColorType && colorType != kBGRA_8888_SkColorType) {
        colorType = kN32_SkColorType;
    }
    SkImageInfo info = SkImageInfo::Make(width, height, colorType, kPremul_SkAlphaType, targetInfo.refColorSpace());
    size_t bytes = info.computeMinByteSize();
    if (!makeRoom(bytes)) {
        return nullptr;
    }
    SkSurfaceProps props = target->getTopProps().cloneWithPixelGeometry(kUnknown_SkPixelGeometry);
    sk_sp<SkSurface> surface = target->makeSurface(info, &props);
    if (!surface) {
        return nullptr;
    }
    s.madeThisFrame++;
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorTRANSPARENT);
    {
        RenderingScope scope;
        draw(canvas);
    }
    auto texture = std::make_unique<NodeTexture>();
    // Graphite: the surface's own image, and the surface let go at once (it becomes immutable and
    // forgets its recorder). Ganesh: a snapshot, which shares the texture once the surface is gone.
    texture->image = owner.graphite ? surface->makeTemporaryImage() : surface->makeImageSnapshot();
    surface.reset();
    if (!texture->image) {
        return nullptr;
    }
    texture->owner = owner;
    texture->thread = std::this_thread::get_id();
    texture->bytes = bytes;
    texture->usedFrame = s.frame;
    texture->usedMs = nowMs();
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        registerLocked(texture.get());
    }
    if (RasterCache::log()) {
        fprintf(stderr, "[skiko] node texture %dx%d (%s), %zu KB in %d textures\n", width, height,
                owner.graphite ? "graphite" : "ganesh", registryBytes.load() >> 10, registryCount.load());
    }
    return texture;
}

void RasterCache::purge(int64_t msNotUsed, const void* owner) {
    std::vector<sk_sp<SkImage>> released;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        auto here = std::this_thread::get_id();
        int64_t now = nowMs();
        for (size_t i = 0; i < registry.size();) {
            NodeTexture* texture = registry[i];
            bool mine = texture->owner.graphite || texture->thread == here;
            if (mine && (!owner || texture->owner.object == owner) && now - texture->usedMs >= msNotUsed) {
                released.push_back(takeImageLocked(texture));
            } else {
                i++;
            }
        }
    }
    drainGraveyard();
}

size_t RasterCache::bytes(const void* owner) {
    if (!owner) {
        return registryBytes.load(std::memory_order_relaxed);
    }
    std::lock_guard<std::mutex> lock(registryMutex);
    size_t sum = 0;
    for (NodeTexture* texture : registry) {
        if (texture->owner.object == owner) {
            sum += texture->bytes;
        }
    }
    return sum;
}

int RasterCache::count() {
    return registryCount.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------------------------------
// Content analysis: what drawing a node's own content costs, and whether it composites the same
// drawn into a transparent texture first: every operation blends source-over (any other mode
// reads or replaces what is under it, which in the texture is nothing), and nothing reads the
// backdrop. The nodes it draws are analysed on their own (see analyseSubtree).
// ---------------------------------------------------------------------------------------------

static bool isSourceOver(const SkPaint* paint) {
    if (!paint || !paint->getBlender()) {
        return true;
    }
    std::optional<SkBlendMode> mode = paint->asBlendMode();
    return mode && *mode == SkBlendMode::kSrcOver;
}

class ContentAnalysisCanvas : public SkNoDrawCanvas {
public:
    ContentAnalysisCanvas() : SkNoDrawCanvas(1 << 20, 1 << 20) {}

    int cost = 0;
    bool layerSafe = true;
    std::vector<RenderNode*> nodes;

protected:
    SaveLayerStrategy getSaveLayerStrategy(const SaveLayerRec& rec) override {
        this->cost += rec.fPaint && rec.fPaint->getImageFilter() ? 12 : 6;
        if (rec.fBackdrop || !isSourceOver(rec.fPaint)) {
            this->layerSafe = false;
        }
        return kNoLayer_SaveLayerStrategy;
    }
    bool onDoSaveBehind(const SkRect*) override {
        this->layerSafe = false;
        return false;
    }
    void onDrawPaint(const SkPaint& paint) override { this->op(1, &paint); }
    void onDrawBehind(const SkPaint& paint) override { this->layerSafe = false; }
    void onDrawRect(const SkRect&, const SkPaint& paint) override { this->op(1, &paint); }
    void onDrawRRect(const SkRRect&, const SkPaint& paint) override { this->op(1, &paint); }
    void onDrawDRRect(const SkRRect&, const SkRRect&, const SkPaint& paint) override { this->op(1, &paint); }
    void onDrawOval(const SkRect&, const SkPaint& paint) override { this->op(1, &paint); }
    void onDrawArc(const SkRect&, SkScalar, SkScalar, bool, const SkPaint& paint) override { this->op(1, &paint); }
    void onDrawPath(const SkPath&, const SkPaint& paint) override { this->op(2, &paint); }
    void onDrawRegion(const SkRegion&, const SkPaint& paint) override { this->op(1, &paint); }
    void onDrawPoints(PointMode, size_t, const SkPoint[], const SkPaint& paint) override { this->op(1, &paint); }
    void onDrawTextBlob(const SkTextBlob*, SkScalar, SkScalar, const SkPaint& paint) override { this->op(3, &paint); }
    void onDrawGlyphRunList(const sktext::GlyphRunList&, const SkPaint& paint) override { this->op(3, &paint); }
    void onDrawSlug(const sktext::gpu::Slug*, const SkPaint& paint) override { this->op(3, &paint); }
    void onDrawImage2(const SkImage*, SkScalar, SkScalar, const SkSamplingOptions&, const SkPaint* paint) override {
        this->op(2, paint);
    }
    void onDrawImageRect2(const SkImage*, const SkRect&, const SkRect&, const SkSamplingOptions&, const SkPaint* paint,
                          SrcRectConstraint) override {
        this->op(2, paint);
    }
    void onDrawImageLattice2(const SkImage*, const Lattice&, const SkRect&, SkFilterMode, const SkPaint* paint) override {
        this->op(2, paint);
    }
    void onDrawAtlas2(const SkImage*, const SkRSXform[], const SkRect[], const SkColor[], int count, SkBlendMode mode,
                      const SkSamplingOptions&, const SkRect*, const SkPaint* paint) override {
        // The colors blend with the sprites in mode; the result blends with the paint's.
        this->op(2, paint);
    }
    void onDrawVerticesObject(const SkVertices*, SkBlendMode, const SkPaint& paint) override { this->op(2, &paint); }
    void onDrawPatch(const SkPoint[12], const SkColor[4], const SkPoint[4], SkBlendMode, const SkPaint& paint) override {
        this->op(2, &paint);
    }
    void onDrawMesh(const SkMesh&, sk_sp<SkBlender>, const SkPaint& paint) override { this->op(2, &paint); }
    void onDrawShadowRec(const SkPath&, const SkDrawShadowRec&) override { this->cost += 4; }
    void onDrawEdgeAAQuad(const SkRect&, const SkPoint[4], QuadAAFlags, const SkColor4f&, SkBlendMode mode) override {
        this->cost += 1;
        if (mode != SkBlendMode::kSrcOver) {
            this->layerSafe = false;
        }
    }
    void onDrawEdgeAAImageSet2(const ImageSetEntry[], int count, const SkPoint[], const SkMatrix[],
                               const SkSamplingOptions&, const SkPaint* paint, SrcRectConstraint) override {
        this->op(2 * std::max(count, 1), paint);
    }
    void onDrawPicture(const SkPicture* picture, const SkMatrix* matrix, const SkPaint* paint) override {
        if (paint) {
            this->getSaveLayerStrategy(SaveLayerRec(nullptr, paint));
        }
        picture->playback(this);
    }
    void onDrawDrawable(SkDrawable* drawable, const SkMatrix* matrix) override {
        if (RenderNode::isRenderNode(drawable)) {
            this->nodes.push_back(static_cast<RenderNode*>(drawable));
            this->cost += 1;
            return;
        }
        // A drawable of unknown content: replayed as it is now.
        drawable->draw(this, matrix);
    }

private:
    void op(int weight, const SkPaint* paint) {
        this->cost += weight;
        if (!isSourceOver(paint)) {
            this->layerSafe = false;
        }
    }
};

// ---------------------------------------------------------------------------------------------
// RenderNode's side.
// ---------------------------------------------------------------------------------------------

static bool sameM44(const SkM44& a, const SkM44& b) {
    float ma[16], mb[16];
    a.getColMajor(ma);
    b.getColMajor(mb);
    return memcmp(ma, mb, sizeof(ma)) == 0;
}

// Whether b is a whole-pixel translation of a (within a thousandth of a pixel, which rasterises to
// the same pixels), and by how much.
static bool wholePixelOffset(const SkM44& a, const SkM44& b, SkIPoint* offset) {
    for (int r = 0; r < 4; r++) {
        for (int c = 0; c < 4; c++) {
            if (c == 3 && r < 2) {
                continue;
            }
            if (a.rc(r, c) != b.rc(r, c)) {
                return false;
            }
        }
    }
    float dx = b.rc(0, 3) - a.rc(0, 3);
    float dy = b.rc(1, 3) - a.rc(1, 3);
    float rx = std::round(dx), ry = std::round(dy);
    if (std::fabs(dx - rx) > 1e-3f || std::fabs(dy - ry) > 1e-3f ||
        std::fabs(rx) > 1e7f || std::fabs(ry) > 1e7f) {
        return false;
    }
    if (offset) {
        *offset = { static_cast<int>(rx), static_cast<int>(ry) };
    }
    return true;
}

// skif's RoundOut, which SkCanvas::internalSaveLayer uses for a layer's bounds.
static SkIRect roundOutLayer(const SkRect& r) {
    static constexpr float kRoundEpsilon = 1e-3f;
    return r.makeInset(kRoundEpsilon, kRoundEpsilon).roundOut();
}

// The device bounds of local under ctm (no perspective), rounded out; false when unbounded.
static bool deviceBounds(const SkM44& ctm, const SkRect& local, SkIRect* result, bool layer) {
    SkMatrix matrix = ctm.asM33();
    if (local.isEmpty() || !local.isFinite() || matrix.hasPerspective() || !matrix.isFinite()) {
        return false;
    }
    SkRect mapped = matrix.mapRect(local);
    if (!mapped.isFinite() || std::fabs(mapped.fLeft) > 1e8f || std::fabs(mapped.fRight) > 1e8f ||
        std::fabs(mapped.fTop) > 1e8f || std::fabs(mapped.fBottom) > 1e8f) {
        return false;
    }
    if (layer) {
        *result = roundOutLayer(mapped);
    } else {
        // Anti-aliased edges reach half a pixel past the geometry.
        mapped.outset(1.0f, 1.0f);
        *result = mapped.roundOut();
    }
    return !result->isEmpty();
}

// The larger of a transform's two scales.
static float maxScale(const SkM44& m) {
    SkMatrix matrix = m.asM33();
    float sx = std::sqrt(matrix.getScaleX() * matrix.getScaleX() + matrix.getSkewY() * matrix.getSkewY());
    float sy = std::sqrt(matrix.getSkewX() * matrix.getSkewX() + matrix.getScaleY() * matrix.getScaleY());
    return std::max(sx, sy);
}

// Whether ctm maps device pixels to device pixels one to one (a translation, or one with quarter
// turns or flips): where a content texture at rest reproduces the direct drawing (an anti-aliased
// clip at another scale rasterises a little differently in a texture's device, on Ganesh).
static bool unitScale(const SkM44& ctm) {
    SkMatrix m = ctm.asM33();
    if (m.hasPerspective()) {
        return false;
    }
    float a = m.getScaleX(), b = m.getSkewX(), c = m.getSkewY(), d = m.getScaleY();
    auto unit = [](float v) { return v == 1.0f || v == -1.0f; };
    return (unit(a) && unit(d) && b == 0.0f && c == 0.0f) || (unit(b) && unit(c) && a == 0.0f && d == 0.0f);
}

static SkPaint restorePaint(const SkPaint& layerPaint) {
    SkPaint paint(layerPaint);
    paint.setStyle(SkPaint::kFill_Style);
    paint.setPathEffect(nullptr);
    paint.setMaskFilter(nullptr);
    paint.setImageFilter(nullptr);
    paint.setAntiAlias(true);
    return paint;
}

void RenderNode::analyseSubtree() {
    NodeCache* cache = this->cache.get();
    if (cache->subtreeAnalysed && cache->subtreeAnalysedGeneration == this->subtreeGeneration) {
        return;
    }
    if (!cache->ownAnalysed || cache->ownAnalysedGeneration != this->ownGeneration) {
        ContentAnalysisCanvas analysis;
        if (this->contentCache) {
            this->contentCache->draw(&analysis);
        }
        cache->ownCost = analysis.cost;
        cache->ownLayerSafe = analysis.layerSafe;
        cache->ownAnalysed = true;
        cache->ownAnalysedGeneration = this->ownGeneration;
    }
    int cost = cache->ownCost + (this->shadowElevation > 0 ? 4 : 0);
    bool safe = cache->ownLayerSafe;
    for (RenderNode* node : this->dependencies) {
        if (!node->cache) {
            node->cache = std::make_unique<NodeCache>();
        }
        node->analyseSubtree();
        cost = std::min(cost + node->cache->subtreeCost + (node->layerPaint ? 6 : 0), 1 << 24);
        if (!node->cache->subtreeLayerSafe ||
            (node->layerPaint && !isSourceOver(&*node->layerPaint)) ||
            (node->layerPaint && node->layerPaint->getImageFilter() == nullptr && !node->layerBatchable)) {
            safe = false;
        }
    }
    cache->subtreeCost = cost;
    cache->subtreeLayerSafe = safe;
    cache->subtreeAnalysed = true;
    cache->subtreeAnalysedGeneration = this->subtreeGeneration;
}

// The local rect of what a node without a layer draws, when its clip bounds it.
bool RenderNode::contentClipBounds(SkRect* local) const {
    if (!this->clip || this->clipOp != SkClipOp::kIntersect) {
        return false;
    }
    if (this->clipRect) {
        *local = *this->clipRect;
    } else if (this->clipRRect) {
        *local = this->clipRRect->getBounds();
    } else if (this->clipPath) {
        if (this->clipPath->isInverseFillType()) {
            return false;
        }
        *local = this->clipPath->getBounds();
    } else {
        *local = SkRect::MakeWH(this->bounds.width(), this->bounds.height());
    }
    return local->isFinite() && !local->isEmpty();
}

bool RenderNode::mayCacheContent() {
    if ((!RasterCache::cacheContent(false) && !RasterCache::cacheContent(true)) || this->layerPaint ||
        this->alpha < 1.0f || !this->contentCache || !this->context->shouldUseSnapshotCache()) {
        return false;
    }
    SkRect local;
    if (!this->contentClipBounds(&local) && !this->context->shouldTrackDamage()) {
        return false;
    }
    if (!this->cache) {
        this->cache = std::make_unique<NodeCache>();
    }
    this->analyseSubtree();
    return this->cache->subtreeLayerSafe && this->cache->subtreeCost >= contentMinCost();
}

void RenderNode::releaseCache() {
    if (this->cache) {
        this->cache->texture.reset();
    }
}

void RenderNode::settle() {
    NodeCache* cache = this->cache.get();
    if (!cache || !cache->unsettled) {
        return;
    }
    cache->unsettled = false;
    if (cache->inexactGeneration == this->propertyGeneration) {
        // Its animation ended on the frame that composited it inexactly: drawn again, exactly,
        // and damaged for that.
        this->changed(kAppearance);
    }
}

// Whether the node's drawing can come from a texture at all, drawn at canvas onto target (the
// same canvas, but for the layer atlas's collect pass): a node that draws its layer (plain or
// image filtered), or its content without a layer and at full alpha.
NodeCache* RenderNode::cacheFor(SkCanvas* canvas, SkCanvas* target, const TextureOwner& owner) {
    if (!owner || RasterCache::bypassed() || RasterCache::rendering() || !this->context->shouldUseSnapshotCache()) {
        return nullptr;
    }
    if (this->layerPaint) {
        if (!RasterCache::retainLayers() || this->layerPaint->getAlphaf() == 0.0f) {
            // Off, or a layer that shows nothing yet (a page about to fade in).
            return nullptr;
        }
        if (!this->layerBatchable) {
            // An image filter layer whose restore is otherwise a plain composite.
            SkPaint unfiltered(*this->layerPaint);
            unfiltered.setImageFilter(nullptr);
            if (!this->layerPaint->getImageFilter() || !isBatchableLayerPaint(unfiltered) ||
                !RasterCache::retainFilteredLayers(owner.graphite)) {
                return nullptr;
            }
        }
    } else if (this->alpha < 1.0f || !this->contentCache ||
               target->getTopProps().pixelGeometry() != kUnknown_SkPixelGeometry) {
        // An alpha modulated through the paints, nothing recorded, or LCD text the target could draw.
        return nullptr;
    }
    SkDevice* device = SkCanvasPriv::TopDevice(canvas);
    if (!device || !(device->deviceToGlobal() == SkM44()) || device->isClipEmpty()) {
        return nullptr;
    }
    if (!this->cache) {
        this->cache = std::make_unique<NodeCache>();
    }
    return this->cache.get();
}

// The frame's decision for drawing this node at canvas (see NodeCache::Decision), made once a
// frame; the bookkeeping of stable and moving draws happens here.
NodeCache::Decision RenderNode::cacheDecision(SkCanvas* canvas, SkCanvas* target, const SkM44& restCtm,
                                              const TextureOwner& owner) {
    NodeCache* cache = this->cacheFor(canvas, target, owner);
    if (!cache) {
        return NodeCache::kDirect;
    }
    const SkM44 ctm = SkCanvasPriv::TopDevice(canvas)->localToDevice44();
    uint64_t frame = RasterCache::frame();
    if (cache->decided && cache->decisionFrame == frame) {
        // Another draw in the same frame: as decided, unless it is drawn elsewhere too, or onto
        // another owner's canvas.
        bool sameOwner = !cache->texture || cache->texture->owner == owner;
        return sameOwner && sameM44(cache->decisionCtm, ctm) ? cache->decision : NodeCache::kDirect;
    }

    bool layer = this->layerPaint.has_value();
    bool filtered = layer && this->layerPaint->getImageFilter() != nullptr;
    uint32_t paintKey = filtered ? this->layerPaintGeneration : 0;
    uint32_t lighting = this->context->getLightingGeneration();
    SkImageInfo targetInfo = target->imageInfo();

    // Bookkeeping: what changed since the last frame's draw.
    bool sameContent = cache->drawn && cache->drawContent == this->subtreeGeneration && cache->drawPaint == paintKey;
    bool moved = cache->drawn && !sameM44(cache->drawCtm, ctm);
    bool consecutive = cache->drawn && cache->drawFrame + 1 == frame;
    if (!sameContent) {
        cache->stableDraws = 1;
        cache->motionFrames = 0;
    } else if (moved) {
        cache->motionFrames = consecutive ? cache->motionFrames + 1 : 1;
        cache->stableDraws = wholePixelOffset(cache->drawCtm, ctm, nullptr) ? cache->stableDraws + 1 : 1;
    } else {
        cache->motionFrames = 0;
        cache->stableDraws++;
    }
    cache->drawn = true;
    cache->drawFrame = frame;
    cache->drawCtm = ctm;
    cache->drawContent = this->subtreeGeneration;
    cache->drawPaint = paintKey;

    NodeCache::Decision decision = NodeCache::kDirect;
    SkIPoint offset = { 0, 0 };
    NodeTexture::Kind kind = !layer ? NodeTexture::kContent : filtered ? NodeTexture::kFilteredLayer : NodeTexture::kLayer;
    SkIRect clip = canvas->getDeviceClipBounds();
    bool stable = cache->stableDraws >= (kStableDraws << cache->churn);

    // A texture of what the node draws now?
    NodeTexture* texture = cache->texture.get();
    auto drop = [&](bool stale) {
        if (stale && texture->image) {
            cache->churn = texture->hits < kPayback ? std::min(cache->churn + 1, kMaxChurn) : std::max(cache->churn - 1, 0);
        }
        cache->texture.reset();
        texture = nullptr;
    };
    if (texture && (!texture->image || texture->kind != kind || texture->owner != owner ||
                    texture->content != this->subtreeGeneration || texture->paint != paintKey ||
                    texture->lighting != lighting || texture->colorType != targetInfo.colorType() ||
                    texture->colorSpace != targetInfo.colorSpace())) {
        drop(texture->content != this->subtreeGeneration);
    }

    // Promotion: a layer (no filter), or a clipping node whose content composites the same
    // through a texture and is worth it, while a host draws settle frames. The content is only
    // analysed once it could matter.
    bool promotable = RasterCache::promote() && RasterCache::hostFrames() && kind != NodeTexture::kFilteredLayer;
    bool contentWorthIt = false;
    if (kind == NodeTexture::kContent) {
        SkRect local;
        bool clipped = this->contentClipBounds(&local);
        promotable = promotable && clipped;
        if (texture || stable || (promotable && cache->motionFrames > 0)) {
            this->analyseSubtree();
            contentWorthIt = cache->subtreeLayerSafe && cache->subtreeCost >= contentMinCost();
        }
        promotable = promotable && contentWorthIt;
        if (texture && !contentWorthIt) {
            drop(false);
        }
    }

    if (texture) {
        if (kind == NodeTexture::kFilteredLayer) {
            // Under the same transform, for a clip within the one it was rendered for.
            SkIRect need;
            if (sameM44(texture->ctm, ctm) && this->filteredLayerBounds(canvas, ctm, &need) &&
                texture->bounds.contains(need)) {
                decision = NodeCache::kExact;
            }
        } else if (sameM44(texture->ctm, ctm) ||
                   (texture->extentComplete && this->contentTransformInvariant && this->shadowElevation <= 0.0f &&
                    wholePixelOffset(texture->ctm, ctm, &offset))) {
            // At the transform it was rendered at, or a whole-pixel translation of it: the same
            // pixels, where they cover what the clip lets through.
            SkIRect need = texture->extent.makeOffset(offset.x(), offset.y());
            if (!need.intersect(clip) || texture->bounds.makeOffset(offset.x(), offset.y()).contains(need)) {
                decision = NodeCache::kExact;
            } else {
                offset = { 0, 0 };
            }
        }
        if (decision == NodeCache::kDirect && promotable && cache->motionFrames > 0 && texture->extentComplete) {
            // Moving: composited at this frame's transform, while it doesn't need more pixels
            // than the texture has.
            SkM44 inverse;
            if (texture->ctm.invert(&inverse)) {
                SkM44 mapping = ctm * inverse;
                float scale = maxScale(mapping);
                if (!mapping.asM33().hasPerspective() && scale <= 1.001f && scale >= 0.125f) {
                    decision = NodeCache::kMapped;
                }
            }
        }
        if (decision == NodeCache::kDirect) {
            // Of no use here: a filtered layer's clip outgrew it (which counts as churn), or the
            // node came to rest elsewhere.
            drop(kind == NodeTexture::kFilteredLayer && sameM44(texture->ctm, ctm));
        }
    }
    if (!texture) {
        offset = { 0, 0 };
        // A node whose textures keep going stale before they pay for themselves waits longer for either kind.
        if (promotable && cache->motionFrames >= (kPromoteAfterFrames << cache->churn) && RasterCache::mayRenderThisFrame()) {
            decision = NodeCache::kMakePromoted;
        } else if (stable && RasterCache::mayRenderThisFrame() &&
                   (kind != NodeTexture::kContent ||
                    (RasterCache::cacheContent(owner.graphite) && contentWorthIt && unitScale(ctm)))) {
            decision = NodeCache::kMakeExact;
        }
    }

    cache->decided = true;
    cache->decisionFrame = frame;
    cache->decisionCtm = ctm;
    cache->decision = decision;
    cache->offset = offset;
    return decision;
}

// The device area an image filter layer's texture holds under ctm: the clip within the filter's
// output (what a damage clip around the whole output leaves the same as a full one).
bool RenderNode::filteredLayerBounds(SkCanvas* canvas, const SkM44& ctm, SkIRect* result) const {
    SkM44 inverse;
    if (!ctm.isFinite() || !ctm.invert(&inverse)) {
        return false;
    }
    SkRect rect = SkRect::MakeWH(this->bounds.width(), this->bounds.height());
    *result = SkCanvasPriv::TopDevice(canvas)->devClipBounds();
    SkRect output = this->layerPaint->getImageFilter()->computeFastBounds(rect);
    SkIRect outputBounds = ctm.asM33().mapRect(output).roundOut();
    outputBounds.outset(1, 1);
    return result->intersect(outputBounds);
}

bool RenderNode::cacheWillDraw(SkCanvas* canvas, SkCanvas* target, const SkM44& restCtm) {
    return this->cacheDecision(canvas, target, restCtm, RasterCache::ownerOf(target)) != NodeCache::kDirect;
}

// Draws the node's layer (or, without a layer, its clip and content) from its texture, making the
// texture first where the frame's decision says so; false where it is drawn otherwise.
bool RenderNode::drawFromCache(SkCanvas* canvas, const SkM44& restCtm) {
    TextureOwner owner = RasterCache::ownerOf(canvas);
    NodeCache::Decision decision = this->cacheDecision(canvas, canvas, restCtm, owner);
    if (decision == NodeCache::kDirect) {
        return false;
    }
    NodeCache* cache = this->cache.get();
    SkDevice* device = SkCanvasPriv::TopDevice(canvas);
    const SkM44 ctm = device->localToDevice44();
    bool layer = this->layerPaint.has_value();
    bool filtered = layer && this->layerPaint->getImageFilter() != nullptr;
    SkRect rect = SkRect::MakeWH(this->bounds.width(), this->bounds.height());

    if (decision == NodeCache::kMakeExact || decision == NodeCache::kMakePromoted) {
        bool promoted = decision == NodeCache::kMakePromoted;
        // The raster transform: this one; for a promotion, the node at rest (its own transform
        // left out) at a scale no smaller than its own, in quarter octaves, so that a scale
        // animation renders once and stays sharp, and comes to rest at its own pixels.
        SkM44 raster = ctm;
        if (promoted) {
            float scale = std::max({ 1.0f, std::fabs(this->scaleX), std::fabs(this->scaleY) });
            if (scale > 1.0f) {
                scale = std::exp2(std::ceil(std::log2(scale) * 4.0f - 1e-4f) / 4.0f);
            }
            raster = restCtm * SkM44::Scale(scale, scale);
        }
        SkIRect area, extent;
        bool complete = true;
        if (filtered) {
            if (!this->filteredLayerBounds(canvas, ctm, &area)) {
                return false;
            }
            extent = area;
        } else {
            SkRect local = rect;
            if (!layer && !this->contentClipBounds(&local)) {
                // A content texture of a node without a clip covers what a damage walk last found
                // it to draw, under this very transform.
                const DamageRecord& record = this->damage;
                if (promoted || record.tracker == 0 || record.multiple || record.bounds.isEmpty() ||
                    record.ownGeneration != this->ownGeneration || record.subtreeGeneration != this->subtreeGeneration ||
                    !sameM44(record.ctm, ctm)) {
                    cache->decision = NodeCache::kDirect;
                    return false;
                }
                extent = record.bounds;
                // Clipped by the walk's clip, it may not be all of the drawing.
                complete = record.clip.makeInset(2, 2).contains(record.bounds);
            } else if (!deviceBounds(raster, local, &extent, layer)) {
                cache->decision = NodeCache::kDirect;
                return false;
            }
            area = extent;
            if (!promoted) {
                // At rest: the part on the target.
                SkISize size = canvas->getBaseLayerSize();
                if (!area.intersect(SkIRect::MakeSize(size))) {
                    cache->decision = NodeCache::kDirect;
                    return false;
                }
            }
        }
        if (!SkIRect::Intersects(extent, canvas->getDeviceClipBounds())) {
            // Draws nothing here: no texture for it yet.
            cache->decision = NodeCache::kDirect;
            return false;
        }
        SkIRect target = filtered ? SkIRect::MakeLTRB(0, 0, area.right(), area.bottom()) : area;
        std::unique_ptr<NodeTexture> texture = RasterCache::render(canvas, owner, target.width(), target.height(),
            [&](SkCanvas* textureCanvas) {
                if (filtered) {
                    // The filtered layer as saveLayer makes it: the same device space and clip
                    // bounds, composited into the texture as is, so it holds the filter's output.
                    textureCanvas->clipRect(SkRect::Make(area));
                    textureCanvas->setMatrix(ctm);
                    SkPaint filterPaint;
                    filterPaint.setImageFilter(this->layerPaint->refImageFilter());
                    textureCanvas->saveLayer(rect, &filterPaint);
                    this->drawLayerContent(textureCanvas);
                    textureCanvas->restore();
                    return;
                }
                // The layer's device as SkCanvas::internalSaveLayer sets it up; for a node
                // without a layer, its clip and content, which the texture then composites
                // source-over as the operations would have (only the canvas's clip applies to it:
                // the node's anti-aliased edges are in the texture once).
                textureCanvas->setMatrix(SkM44::Translate(static_cast<float>(-area.left()),
                                                          static_cast<float>(-area.top())) * raster);
                if (!layer) {
                    this->applyClip(textureCanvas);
                }
                this->drawLayerContent(textureCanvas);
            });
        if (!texture) {
            cache->decision = NodeCache::kDirect;
            return false;
        }
        texture->kind = !layer ? NodeTexture::kContent : filtered ? NodeTexture::kFilteredLayer : NodeTexture::kLayer;
        texture->ctm = raster;
        texture->bounds = area;
        texture->extent = extent;
        texture->extentComplete = complete;
        texture->origin = filtered ? SkIPoint{ 0, 0 } : SkIPoint{ area.left(), area.top() };
        texture->promoted = promoted;
        texture->content = this->subtreeGeneration;
        texture->paint = filtered ? this->layerPaintGeneration : 0;
        texture->lighting = this->context->getLightingGeneration();
        texture->colorType = canvas->imageInfo().colorType();
        texture->colorSpace = canvas->imageInfo().colorSpace();
        cache->texture = std::move(texture);
        bool exact = sameM44(raster, ctm);
        cache->decision = exact ? NodeCache::kExact : NodeCache::kMapped;
        cache->offset = { 0, 0 };
        decision = cache->decision;
    }

    NodeTexture* texture = cache->texture.get();
    SkPaint paint = layer ? restorePaint(*this->layerPaint) : SkPaint();
    canvas->save();
    if (decision == NodeCache::kExact) {
        SkIRect bounds = texture->bounds;
        if (filtered) {
            // Outside the filter's output the texture is transparent, which the paint leaves as is.
            SkRect output = this->layerPaint->getImageFilter()->computeFastBounds(rect);
            SkIRect outputBounds = ctm.asM33().mapRect(output).roundOut();
            outputBounds.outset(1, 1);
            if (!bounds.intersect(outputBounds)) {
                canvas->restore();
                RasterCache::used(texture);
                return true;
            }
        }
        SkRect src = SkRect::Make(bounds.makeOffset(-texture->origin.x(), -texture->origin.y()));
        SkRect dst = SkRect::Make(bounds.makeOffset(cache->offset.x(), cache->offset.y()));
        canvas->resetMatrix();
        canvas->drawImageRect(texture->image.get(), src, dst, SkSamplingOptions(), &paint,
                              SkCanvas::kFast_SrcRectConstraint);
    } else {
        // Composited at this frame's transform, filtered.
        SkM44 inverse;
        (void) texture->ctm.invert(&inverse);  // invertible: checked by the decision
        canvas->setMatrix(ctm * inverse);
        SkRect src = SkRect::MakeIWH(texture->bounds.width(), texture->bounds.height());
        canvas->drawImageRect(texture->image.get(), src, SkRect::Make(texture->bounds),
                              SkSamplingOptions(SkFilterMode::kLinear), &paint, SkCanvas::kFast_SrcRectConstraint);
        cache->unsettled = true;
        cache->inexactGeneration = this->propertyGeneration;
        RasterCache::drewInexact(this);
    }
    canvas->restore();
    RasterCache::used(texture);
    return true;
}

} // namespace node
} // namespace skiko

// For the Graphite extension's bindings, which see the core's C symbols only.
#if defined(_WIN32)
#define SKIKO_NODE_C_API extern "C" __declspec(dllexport)
#else
#define SKIKO_NODE_C_API extern "C" __attribute__((visibility("default")))
#endif

SKIKO_NODE_C_API void skiko_node_onGraphiteRecorderClosing(const void* recorder) {
    skiko::node::RasterCache::onGraphiteRecorderClosing(recorder);
}

SKIKO_NODE_C_API void skiko_node_purgeTextures(int64_t msNotUsed, const void* owner) {
    skiko::node::RasterCache::purge(msNotUsed, owner);
}
