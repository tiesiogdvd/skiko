#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <atomic>
#include <cstring>
#include <mutex>
#include <thread>
#include <unordered_map>
#include <vector>
#include <SkBlendMode.h>
#include <SkColorFilter.h>
#include <SkImage.h>
#include <SkMaskFilter.h>
#include <SkPathEffect.h>
#include <SkNoDrawCanvas.h>
#include <SkNWayCanvas.h>
#include <SkPathBuilder.h>
#include <SkPaintFilterCanvas.h>
#include <SkPicture.h>
#include <SkShadowUtils.h>
#include <SkSurface.h>
#include "src/core/SkCanvasPriv.h"
#include "src/core/SkDevice.h"
#include "node/RenderNode.h"
#include "node/RenderNodeContext.h"

namespace skiko {
namespace node {

// The goal with selecting the size of the rectangle here is to avoid limiting the
// drawable area as much as possible.
// Due to https://issuetracker.google.com/issues/324465764 we have to
// leave room for scale between the values we specify here and Float.MAX_VALUE.
// The maximum possible scale that can be applied to the canvas will be
// Float.MAX_VALUE divided by the largest value below.
// 2^30 was chosen because it's big enough, leaves quite a lot of room between it
// and Float.MAX_VALUE, and also lets the width and height fit into int32 (just in
// case).
static const float UNKNOWN_BOUNDS_MIN_VALUE = static_cast<float>(-(1L << 30));
static const float UNKNOWN_BOUNDS_MAX_VALUE = static_cast<float>((1L << 30) - 1);
static SkRect UNKNOWN_BOUNDS {
    UNKNOWN_BOUNDS_MIN_VALUE,
    UNKNOWN_BOUNDS_MIN_VALUE,
    UNKNOWN_BOUNDS_MAX_VALUE,
    UNKNOWN_BOUNDS_MAX_VALUE
};

static const float DEFAULT_CAMERA_DISTANCE = 8.0f;
static const float NON_ZERO_EPSILON = 0.001f;

// Since "kSkDrawable_Type" isn't really used anywhere outside of serialization, we can use it
// to identify RenderNode objects without RTTI, like SkRuntimeEffect does (even without adding to original enum).
// The value has to stay outside the range Skia assigns, or a node is mistaken for a
// flattenable of that type. kSkShader_Type is the largest Skia declares.
static const SkFlattenable::Type kRenderNode_Type = static_cast<SkFlattenable::Type>(0x2d2595b6);

/**
 * Check for floats that are close enough to zero.
 */
inline static bool isZero(float value) {
    // Using fabsf is more performant as ARM computes
    // fabsf in a single instruction.
    return fabsf(value) <= NON_ZERO_EPSILON;
}

static SkColor multiplyAlpha(SkColor color, float alpha) {
    return SkColorSetA(color, alpha * SkColorGetA(color));
}

// Set while a snapshot recording replays content, so that the nodes it reaches know their
// drawing is being inlined; and while the recording inlines everything (a picture
// snapshot), or the content is replayed through an alpha filter canvas, where nodes draw
// their picture snapshots as they always have.
static thread_local int inlineDepth = 0;
static thread_local int inlineEverythingDepth = 0;

struct DepthScope {
    explicit DepthScope(int& depth) : depth(depth) { depth++; }
    ~DepthScope() { depth--; }
    int& depth;
};

// A live snapshot: a recording of a node's content in which nodes that should be referenced
// are drawn live. A recording that reaches it references it (it is immutable).
static const SkFlattenable::Type kContentSnapshot_Type = static_cast<SkFlattenable::Type>(0x2d2595b7);
// A reference to a node from a live snapshot.
static const SkFlattenable::Type kNodeReference_Type = static_cast<SkFlattenable::Type>(0x2d2595b8);

class ContentSnapshot : public SkDrawable {
public:
    ContentSnapshot(sk_sp<SkDrawable> recording, const SkRect& bounds)
        : recording(std::move(recording)), bounds(bounds) {}

    SkFlattenable::Type getFlattenableType() const override { return kContentSnapshot_Type; }

protected:
    SkRect onGetBounds() override { return this->bounds; }
    size_t onApproximateBytesUsed() override { return this->recording->approximateBytesUsed(); }
    void onDraw(SkCanvas* canvas) override { this->recording->draw(canvas); }

private:
    sk_sp<SkDrawable> recording;
    SkRect bounds;
};

class NodeReference : public SkDrawable {
public:
    explicit NodeReference(sk_sp<SkDrawable> node) : node(std::move(node)) {}

    SkFlattenable::Type getFlattenableType() const override { return kNodeReference_Type; }

protected:
    // The node changes after the recording that holds this was made, so this must never be
    // culled by the bounds the recording knew.
    SkRect onGetBounds() override;
    // The node's own figure is reported by the node.
    size_t onApproximateBytesUsed() override { return 0; }
    void onDraw(SkCanvas* canvas) override { this->node->draw(canvas); }

private:
    sk_sp<SkDrawable> node;
};

class UnrollDrawableCanvas : public SkNWayCanvas {
public:
    // A referencing canvas records references to the nodes that should be referenced and to
    // live snapshots; otherwise it inlines every drawable.
    UnrollDrawableCanvas(SkCanvas* canvas, bool referencing = false)
        : SkNWayCanvas(canvas->imageInfo().width(), canvas->imageInfo().height()), referencing(referencing) {
        this->addCanvas(canvas);
    }

protected:
    void onDrawDrawable(SkDrawable* drawable, const SkMatrix* matrix) override;

private:
    bool referencing;
};

SkRect NodeReference::onGetBounds() {
    return UNKNOWN_BOUNDS;
}

class AlphaModulateFilterCanvas : public SkPaintFilterCanvas {
public:
    AlphaModulateFilterCanvas(SkCanvas* canvas, float alpha)
        : SkPaintFilterCanvas(canvas), alpha(alpha) {}

protected:
    bool onFilter(SkPaint& paint) const override {
        paint.setAlphaf(paint.getAlphaf() * alpha);
        return true;
    }

    void onDrawDrawable(SkDrawable* drawable, const SkMatrix* matrix) override {
        drawable->draw(this, matrix);
    }

private:
    float alpha;
};

void UnrollDrawableCanvas::onDrawDrawable(SkDrawable* drawable, const SkMatrix* matrix) {
    if (this->referencing) {
        SkFlattenable::Type type = drawable->getFlattenableType();
        if (type == kRenderNode_Type) {
            auto renderNode = static_cast<RenderNode *>(drawable);
            if (renderNode->shouldBeReferenced()) {
                auto reference = sk_make_sp<NodeReference>(sk_ref_sp(drawable));
                this->SkNWayCanvas::onDrawDrawable(reference.get(), matrix);
                return;
            }
        } else if (type == kContentSnapshot_Type) {
            this->SkNWayCanvas::onDrawDrawable(drawable, matrix);
            return;
        }
    }
    drawable->draw(this, matrix);
}

// Collects the render nodes drawn by a recording, taking a reference to each.
// The set is expected to be empty on entry.
class DependencyTrackerCanvas : public SkNoDrawCanvas {
public:
    DependencyTrackerCanvas(std::unordered_set<RenderNode *>& dependencies)
        : SkNoDrawCanvas(INT_MAX, INT_MAX), dependencies(dependencies) {}

protected:
    void onDrawDrawable(SkDrawable* drawable, const SkMatrix* matrix) override {
        if (drawable->getFlattenableType() == kRenderNode_Type) {
            auto renderNode = static_cast<RenderNode *>(drawable);
            if (dependencies.insert(renderNode).second) {
                renderNode->ref();
            }
        } else {
            drawable->draw(this, matrix);
        }
    }

private:
    std::unordered_set<RenderNode *>& dependencies;
};


// ---------------------------------------------------------------------------------------------
// Layer atlas.
//
// A node with a layer paint draws its content into a layer of its own (saveLayer), and on a GPU
// each layer is a render pass of its own: a frame with a hundred fading cards renders a hundred
// small passes, whose fixed cost dominates the frame's GPU time and adds CPU work for each pass.
//
// When a tree is drawn onto a GPU canvas, the outermost node drawing it first walks the tree
// (collect), noting each layer it will draw outside any other layer, with the transform and
// the device bounds it will be drawn at. Then it renders all those layers' content into one
// atlas texture (render), one pass for all of them, and draws the tree, where each of those
// layers is a draw of its atlas region with the layer paint (draw) instead of a saveLayer.
//
// This reproduces saveLayer: the region holds what the layer's device would (the content at
// the same device transform, clipped only to the layer's bounds, which are computed as
// SkCanvas::internalSaveLayer computes them), and it is composited the way internalRestore
// composites a layer (the same paint, pixel aligned, nearest sampling, under the canvas's
// clip). A layer whose transform or bounds turn out to differ from what the walk found, or
// that is drawn where saveLayer could map differently (inside another layer), falls back to
// saveLayer. The atlas lives only for the drawing that made it.
// ---------------------------------------------------------------------------------------------

static bool layerBatchingEnabled() {
    static const bool enabled = [] {
        const char* value = getenv("SKIKO_RENDER_NODE_LAYER_ATLAS");
        return !(value && strcmp(value, "0") == 0);
    }();
    return enabled;
}

// SKIKO_RENDER_NODE_LAYER_ATLAS=log prints what the atlas did, every 120th batch.
static bool layerBatchingLog() {
    static const bool log = [] {
        const char* value = getenv("SKIKO_RENDER_NODE_LAYER_ATLAS");
        return value && strcmp(value, "log") == 0;
    }();
    return log;
}

static bool isGpuCanvas(SkCanvas* canvas) {
    return canvas->recordingContext() != nullptr || canvas->recorder() != nullptr;
}

// skif's RoundOut, which SkCanvas::internalSaveLayer uses for a layer's bounds.
static SkIRect layerRoundOut(const SkRect& r) {
    static constexpr float kRoundEpsilon = 1e-3f;
    return r.makeInset(kRoundEpsilon, kRoundEpsilon).roundOut();
}

// The device bounds of a saveLayer(rect) at ctm, before intersecting with the clip.
static bool layerContentBounds(const SkM44& ctm, const SkRect& rect, SkIRect* bounds) {
    SkM44 inverse;
    if (!ctm.isFinite() || !ctm.invert(&inverse) || rect.isEmpty()) {
        return false;
    }
    *bounds = layerRoundOut(ctm.asM33().mapRect(rect));
    return true;
}

static bool sameMatrix(const SkM44& a, const SkM44& b) {
    float ma[16], mb[16];
    a.getColMajor(ma);
    b.getColMajor(mb);
    return memcmp(ma, mb, sizeof(ma)) == 0;
}

// Whether restoring a layer drawn with this paint only composites the layer's own pixels,
// so that the layer can be bounded by its content (internalSaveLayer's trivialRestore) and
// stand alone in an atlas.
static bool isBatchableLayerPaint(const SkPaint& paint) {
    if (paint.getImageFilter() || paint.getShader() || paint.getMaskFilter() || paint.getPathEffect()) {
        return false;
    }
    if (SkColorFilter* cf = paint.getColorFilter()) {
        SkColor4f transparent = cf->filterColor4f(SkColors::kTransparent, nullptr, nullptr);
        if (transparent.fA != 0.0f) {
            return false;
        }
    }
    if (paint.getBlender()) {
        std::optional<SkBlendMode> mode = paint.asBlendMode();
        if (!mode) {
            return false;
        }
        SkBlendModeCoeff src, dst;
        if (SkBlendMode_AsCoeff(*mode, &src, &dst) &&
            dst != SkBlendModeCoeff::kOne && dst != SkBlendModeCoeff::kISA && dst != SkBlendModeCoeff::kISC) {
            return false;
        }
    }
    return true;
}

// The collect pass's canvas: draws nothing, but replays pictures and drawables (so the walk
// reaches every node) and counts the layers it is inside.
class LayerCollectorCanvas : public SkNoDrawCanvas {
public:
    LayerCollectorCanvas(int width, int height) : SkNoDrawCanvas(width, height) {}

    int layerDepth() const { return this->depth; }

protected:
    void willSave() override {
        this->saveIsLayer.push_back(false);
    }
    SaveLayerStrategy getSaveLayerStrategy(const SaveLayerRec& rec) override {
        this->saveIsLayer.push_back(true);
        this->depth++;
        return SkNoDrawCanvas::getSaveLayerStrategy(rec);
    }
    bool onDoSaveBehind(const SkRect* subset) override {
        this->saveIsLayer.push_back(false);
        return SkNoDrawCanvas::onDoSaveBehind(subset);
    }
    void willRestore() override {
        if (!this->saveIsLayer.empty()) {
            if (this->saveIsLayer.back()) {
                this->depth--;
            }
            this->saveIsLayer.pop_back();
        }
    }
    void onDrawDrawable(SkDrawable* drawable, const SkMatrix* matrix) override {
        drawable->draw(this, matrix);
    }
    void onDrawPicture(const SkPicture* picture, const SkMatrix* matrix, const SkPaint* paint) override {
        int saveCount = this->getSaveCount();
        if (paint) {
            SkRect cull = picture->cullRect();
            this->saveLayer(&cull, paint);
        } else {
            this->save();
        }
        if (matrix) {
            this->concat(*matrix);
        }
        picture->playback(this);
        this->restoreToCount(saveCount);
    }

private:
    std::vector<bool> saveIsLayer;
    int depth = 0;
};

class LayerBatch {
public:
    explicit LayerBatch(SkCanvas* target) : target(target), phase(kCollect) {
        current = this;
    }
    ~LayerBatch() {
        current = nullptr;
        if (layerBatchingLog()) {
            static int batches = 0;
            if (batches++ % 120 == 0) {
                int used = 0;
                for (auto& request : this->requests) used += request.used ? 1 : 0;
                fprintf(stderr, "[skiko] layer atlas: %zu layers found, %d drawn from %zu atlas(es)\n",
                        this->requests.size(), used, this->atlases.size());
            }
        }
    }

    static LayerBatch* get() { return current; }

    // Whether a drawing of a node onto canvas should start a batch: a GPU canvas whose top
    // device maps to the canvas's coordinates as is, as only then is a layer's device the
    // canvas's own device space.
    static bool shouldStart(SkCanvas* canvas) {
        if (!layerBatchingEnabled() || !isGpuCanvas(canvas)) {
            return false;
        }
        SkDevice* device = SkCanvasPriv::TopDevice(canvas);
        return device && device->deviceToGlobal() == SkM44() && !device->isClipEmpty();
    }

    bool isCollecting(SkCanvas* canvas) const {
        return this->phase == kCollect && canvas == this->collector.get();
    }

    void collect(RenderNode* root) {
        SkISize size = this->target->getBaseLayerSize();
        this->rootClip = this->target->getDeviceClipBounds();
        this->collector = std::make_unique<LayerCollectorCanvas>(size.width(), size.height());
        this->collector->clipRect(SkRect::Make(this->rootClip));
        this->collector->setMatrix(this->target->getLocalToDevice());
        root->draw(this->collector.get());
        this->collector.reset();
    }

    void request(RenderNode* node, SkCanvas* canvas, const SkRect& rect) {
        if (this->collector->layerDepth() != 0) {
            return;
        }
        SkM44 ctm = canvas->getLocalToDevice();
        SkIRect bounds;
        if (!layerContentBounds(ctm, rect, &bounds)) {
            return;
        }
        // The walk's clip is conservative, but computed apart from the device's: allow a
        // pixel either way, the drawing checks the real bounds against these.
        SkIRect clip = canvas->getDeviceClipBounds();
        clip.outset(1, 1);
        if (!bounds.intersect(clip) || !bounds.intersect(this->rootClip)) {
            return;
        }
        if (bounds.width() > kMaxAtlasSize || bounds.height() > kMaxAtlasSize) {
            return;
        }
        this->byNode[node].push_back(this->requests.size());
        this->requests.push_back({ node, ctm, bounds, -1, {0, 0} });
    }

    // Packs the requested layers into atlases and renders their content.
    void render() {
        this->phase = kRender;
        if (this->requests.empty()) {
            this->phase = kDraw;
            return;
        }
        std::vector<size_t> order(this->requests.size());
        int widest = 0;
        for (size_t i = 0; i < order.size(); i++) {
            order[i] = i;
            widest = std::max(widest, this->requests[i].bounds.width());
        }
        std::stable_sort(order.begin(), order.end(), [this](size_t a, size_t b) {
            return this->requests[a].bounds.height() > this->requests[b].bounds.height();
        });
        int atlasWidth = kMinAtlasWidth;
        while (atlasWidth < widest) {
            atlasWidth *= 2;
        }

        // Shelf packing, tallest first.
        std::vector<int> atlasHeights;
        int x = 0, y = 0, shelfHeight = 0;
        for (size_t i : order) {
            Request& request = this->requests[i];
            int w = request.bounds.width(), h = request.bounds.height();
            if (x + w > atlasWidth) {
                x = 0;
                y += shelfHeight;
                shelfHeight = 0;
            }
            if (atlasHeights.empty() || y + h > kMaxAtlasSize) {
                atlasHeights.push_back(0);
                x = 0;
                y = 0;
                shelfHeight = 0;
            }
            request.atlas = static_cast<int>(atlasHeights.size()) - 1;
            request.origin = { x, y };
            x += w;
            shelfHeight = std::max(shelfHeight, h);
            atlasHeights.back() = std::max(atlasHeights.back(), y + h);
        }

        // A layer has the color type SkCanvas::internalSaveLayer gives it.
        SkImageInfo targetInfo = this->target->imageInfo();
        SkColorType colorType = targetInfo.colorType();
        if (SkColorTypeBytesPerPixel(colorType) <= 4 &&
            colorType != kRGBA_8888_SkColorType && colorType != kBGRA_8888_SkColorType) {
            colorType = kN32_SkColorType;
        }
        SkSurfaceProps props = this->target->getTopProps().cloneWithPixelGeometry(kUnknown_SkPixelGeometry);
        for (int height : atlasHeights) {
            // Rounded up so that a frame's atlas mostly fits the previous frame's texture.
            int roundedHeight = std::min(kMaxAtlasSize, (height + kAtlasHeightStep - 1) / kAtlasHeightStep * kAtlasHeightStep);
            SkImageInfo info = SkImageInfo::Make(
                atlasWidth, roundedHeight, colorType, kPremul_SkAlphaType, targetInfo.refColorSpace());
            sk_sp<SkSurface> surface = this->target->makeSurface(info, &props);
            if (surface) {
                surface->getCanvas()->clear(SK_ColorTRANSPARENT);
            }
            this->atlases.push_back(std::move(surface));
        }

        for (Request& request : this->requests) {
            SkSurface* surface = this->atlases[request.atlas].get();
            if (!surface) {
                continue;
            }
            SkCanvas* canvas = surface->getCanvas();
            canvas->save();
            canvas->clipRect(SkRect::Make(SkIRect::MakeXYWH(
                request.origin.x(), request.origin.y(), request.bounds.width(), request.bounds.height())));
            canvas->setMatrix(SkM44::Translate(
                static_cast<float>(request.origin.x() - request.bounds.left()),
                static_cast<float>(request.origin.y() - request.bounds.top())) * request.ctm);
            request.node->drawLayerContent(canvas);
            canvas->restore();
        }
        for (auto& surface : this->atlases) {
            this->images.push_back(surface ? surface->makeTemporaryImage() : nullptr);
        }
        this->phase = kDraw;
    }

    // Draws node's layer from the atlas, as canvas->saveLayer(rect, &layerPaint), the content
    // and the restore would. False where it can't, leaving the layer to the caller.
    bool composite(RenderNode* node, SkCanvas* canvas, const SkPaint& layerPaint, const SkRect& rect) {
        if (this->phase != kDraw || canvas != this->target) {
            return false;
        }
        auto it = this->byNode.find(node);
        if (it == this->byNode.end()) {
            return false;
        }
        SkDevice* device = SkCanvasPriv::TopDevice(canvas);
        if (!device || !(device->deviceToGlobal() == SkM44())) {
            return false;
        }
        if (device->isClipEmpty()) {
            // saveLayer draws nothing under an empty clip.
            return true;
        }
        const SkM44& ctm = device->localToDevice44();
        for (size_t index : it->second) {
            Request& request = this->requests[index];
            if (request.used || !sameMatrix(request.ctm, ctm)) {
                continue;
            }
            SkIRect bounds;
            if (!layerContentBounds(ctm, rect, &bounds) || !bounds.intersect(device->devClipBounds())) {
                // saveLayer skips a layer that has no bounds.
                return true;
            }
            if (!request.bounds.contains(bounds)) {
                return false;
            }
            SkImage* image = request.atlas >= 0 ? this->images[request.atlas].get() : nullptr;
            if (!image) {
                return false;
            }
            request.used = true;
            // internalSaveLayer's restore paint.
            SkPaint paint(layerPaint);
            paint.setStyle(SkPaint::kFill_Style);
            paint.setPathEffect(nullptr);
            paint.setMaskFilter(nullptr);
            paint.setImageFilter(nullptr);
            paint.setAntiAlias(true);
            SkRect src = SkRect::Make(SkIRect::MakeXYWH(
                request.origin.x() + bounds.left() - request.bounds.left(),
                request.origin.y() + bounds.top() - request.bounds.top(),
                bounds.width(), bounds.height()));
            canvas->save();
            canvas->resetMatrix();
            canvas->drawImageRect(image, src, SkRect::Make(bounds), SkSamplingOptions(), &paint,
                                  SkCanvas::kFast_SrcRectConstraint);
            canvas->restore();
            return true;
        }
        return false;
    }

private:
    enum Phase { kCollect, kRender, kDraw };
    struct Request {
        RenderNode* node;
        SkM44 ctm;
        SkIRect bounds;
        int atlas;
        SkIPoint origin;
        bool used = false;
    };

    static constexpr int kMinAtlasWidth = 2048;
    static constexpr int kMaxAtlasSize = 4096;
    static constexpr int kAtlasHeightStep = 256;

    static thread_local LayerBatch* current;

    SkCanvas* target;
    Phase phase;
    SkIRect rootClip;
    std::unique_ptr<LayerCollectorCanvas> collector;
    std::vector<Request> requests;
    std::unordered_map<RenderNode*, std::vector<size_t>> byNode;
    std::vector<sk_sp<SkSurface>> atlases;
    std::vector<sk_sp<SkImage>> images;
};

thread_local LayerBatch* LayerBatch::current = nullptr;


// ---------------------------------------------------------------------------------------------
// Retained layers.
//
// A layer whose content and placement do not change between frames (a static page fading in,
// a disabled control at a fixed alpha, a blurred panel over static content) is rendered once
// into a texture the node keeps, and each later frame only composites that texture with the
// layer paint, as Android keeps a promoted layer. The texture is the layer saveLayer would make
// -- the same device bounds, the same device transform, so the same pixels -- and it is kept
// only while the key below holds. A layer gets one after a few identical draws in a row, and a
// layer whose key changes goes back to the atlas or saveLayer and frees its texture. An image filter layer (a blur) keeps the
// filtered result, which saveLayer would composite into the canvas, keyed on the canvas's clip
// too, since a filter's input depends on it.
//
// Ganesh only: a Graphite surface keeps a bare pointer to its Recorder, which may go away while
// a node keeps the texture. Textures are bounded by a budget, and one made on a thread is
// released there (a node released elsewhere hands it over).
// ---------------------------------------------------------------------------------------------

static bool layerRetainingEnabled() {
    static const bool enabled = [] {
        const char* value = getenv("SKIKO_RENDER_NODE_LAYER_RETAIN");
        return !(value && strcmp(value, "0") == 0);
    }();
    return enabled;
}

static size_t layerRetainingBudget() {
    static const size_t budget = [] {
        const char* value = getenv("SKIKO_RENDER_NODE_LAYER_RETAIN_MB");
        long mb = value ? strtol(value, nullptr, 10) : 96;
        return static_cast<size_t>(std::max(0L, mb)) << 20;
    }();
    return budget;
}

static std::atomic<size_t> retainedLayerBytes{0};

// A layer gets a texture once it was drawn the same this many times in a row (doubled for each
// time its texture went stale within kRetainedLayerPayback composites).
static constexpr int kRetainedLayerStableDraws = 3;
static constexpr int kRetainedLayerPayback = 4;
static constexpr int kRetainedLayerMaxChurn = 5;

struct RetainedLayerKey {
    const void* context = nullptr;
    SkM44 ctm;
    SkIRect bounds = SkIRect::MakeEmpty();
    uint32_t content = 0;
    uint32_t paint = 0;
    uint32_t lighting = 0;
    SkColorType colorType = kUnknown_SkColorType;
    const SkColorSpace* colorSpace = nullptr;

    bool operator==(const RetainedLayerKey& other) const {
        float a[16], b[16];
        this->ctm.getColMajor(a);
        other.ctm.getColMajor(b);
        return this->context == other.context && this->bounds == other.bounds &&
               this->content == other.content && this->paint == other.paint &&
               this->lighting == other.lighting && this->colorType == other.colorType &&
               this->colorSpace == other.colorSpace && memcmp(a, b, sizeof(a)) == 0;
    }
};

struct RetainedLayer {
    RetainedLayerKey lastKey;
    bool hasLastKey = false;
    // Draws in a row with lastKey, composites from the current texture, and how often a texture
    // went stale before it paid for itself: a layer that keeps changing after short pauses has to
    // hold still for longer (exponentially) before it gets a texture again.
    int stableDraws = 0;
    int hits = 0;
    int churn = 0;
    RetainedLayerKey key;
    sk_sp<SkSurface> surface;
    sk_sp<SkImage> image;
    // Where the image's (0, 0) is on the device.
    SkIPoint origin = { 0, 0 };
    size_t bytes = 0;
    std::thread::id thread;

    void release();
    ~RetainedLayer() { this->release(); }
};

// Textures of retained layers released away from the thread that made them, released there on
// its next draw.
static std::mutex graveyardMutex;
static std::vector<std::pair<std::thread::id, sk_sp<SkSurface>>> graveyard;
static std::atomic<bool> graveyardNonEmpty{false};

static void drainGraveyard() {
    if (!graveyardNonEmpty.load(std::memory_order_relaxed)) {
        return;
    }
    std::vector<sk_sp<SkSurface>> mine;
    {
        std::lock_guard<std::mutex> lock(graveyardMutex);
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
}

void RetainedLayer::release() {
    this->image.reset();
    if (this->surface) {
        retainedLayerBytes.fetch_sub(this->bytes, std::memory_order_relaxed);
        if (std::this_thread::get_id() != this->thread) {
            std::lock_guard<std::mutex> lock(graveyardMutex);
            graveyard.emplace_back(this->thread, std::move(this->surface));
            graveyardNonEmpty.store(true, std::memory_order_relaxed);
        }
        this->surface.reset();
    }
    this->bytes = 0;
}

// The restore paint of a layer whose image (filtered or not) is composited from a texture.
static SkPaint layerRestorePaint(const SkPaint& layerPaint) {
    SkPaint paint(layerPaint);
    paint.setStyle(SkPaint::kFill_Style);
    paint.setPathEffect(nullptr);
    paint.setMaskFilter(nullptr);
    paint.setImageFilter(nullptr);
    paint.setAntiAlias(true);
    return paint;
}

// Whether two layer paints differ only in what a retained layer applies while compositing:
// alpha, color filter and blend mode for a plain layer; for an image filter layer the filter
// has to be the same object.
static bool sameRetainedLayerPaint(const SkPaint& a, const SkPaint& b) {
    return a.getImageFilter() == b.getImageFilter() && a.getShader() == b.getShader() &&
           a.getMaskFilter() == b.getMaskFilter() && a.getPathEffect() == b.getPathEffect();
}

RenderNode::RenderNode(const sk_sp<RenderNodeContext>& context)
    : context(context),
      bbhFactory(context->shouldMeasureDrawBounds() ? new SkRTreeFactory() : nullptr),
      recorder(),
      contentCache(),
      contentSnapshot(),
      contentLiveSnapshot(),
      contentTransformInvariant(true),
      observersNotified(false),
      inlinedSinceNotify(false),
      changesAfterInline(0),
      volatileNode(false),
      dependencies(),
      observers(),
      layerPaint(),
      layerBatchable(false),
      layerPaintGeneration(0),
      subtreeGeneration(0),
      subtreeChangeRelayed(false),
      retainedLayer(),
      layerInfoDirty(true),
      subtreeHasBatchableLayer(false),
      bounds { 0.0f, 0.0f, 0.0f, 0.0f },
      pivot { SK_FloatNaN, SK_FloatNaN },
      alpha(1.0f),
      scaleX(1.0f),
      scaleY(1.0f),
      translationX(0.0f),
      translationY(0.0f),
      shadowElevation(0.0f),
      ambientShadowColor(SK_ColorBLACK),
      spotShadowColor(SK_ColorBLACK),
      rotationX(0.0f),
      rotationY(0.0f),
      rotationZ(0.0f),
      clipRect(),
      clipRRect(),
      clipPath(),
      clipOp(SkClipOp::kIntersect),
      clipAntiAlias(false),
      clip(false),
      transformMatrix(),
      transformCamera(),
      matrixIdentity(true),
      matrixDirty(false) {
    this->setCameraLocation(0.0f, 0.0f, DEFAULT_CAMERA_DISTANCE);
}

RenderNode::~RenderNode() {
    if (this->bbhFactory) {
        delete this->bbhFactory;
        this->bbhFactory = nullptr;
    }
    // The observers set is necessarily empty: an observer holds a reference to this
    // node through its own dependencies, so it cannot outlive it.
    this->releaseDependencies();
}

void RenderNode::setLayerPaint(const std::optional<SkPaint>& layerPaint) {
    bool wasBatchable = this->layerBatchable;
    bool hadLayer = this->layerPaint.has_value();
    if (!hadLayer || !layerPaint || !sameRetainedLayerPaint(*this->layerPaint, *layerPaint)) {
        this->layerPaintGeneration++;
    }
    this->layerPaint = layerPaint;
    this->layerBatchable = layerPaint && isBatchableLayerPaint(*layerPaint);
    if (!layerPaint) {
        this->releaseRetainedLayer();
    }
    if (this->layerBatchable != wasBatchable || layerPaint.has_value() != hadLayer) {
        this->markLayerInfoDirty();
    }
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setBounds(const SkRect& bounds) {
    bool sizeChanged = this->bounds.width() != bounds.width() || this->bounds.height() != bounds.height();

    this->bounds = bounds;

    // Skip matrix re-calculation for topLeft-only changes, since topLef is irrelevant for it
    if (sizeChanged) {
        this->matrixDirty = true;
    }
    // The bounds size is the cull rect of a clipping node's recording.
    this->invalidateSnapshot(sizeChanged ? kContent : kAppearance);
}

void RenderNode::setPivot(const SkPoint& pivot) {
    this->pivot = pivot;
    this->matrixDirty = true;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setAlpha(float alpha) {
    this->alpha = alpha;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setScaleX(float scaleX) {
    this->scaleX = scaleX;
    this->matrixDirty = true;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setScaleY(float scaleY) {
    this->scaleY = scaleY;
    this->matrixDirty = true;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setTranslationX(float translationX) {
    this->translationX = translationX;
    this->matrixDirty = true;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setTranslationY(float translationY) {
    this->translationY = translationY;
    this->matrixDirty = true;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setShadowElevation(float shadowElevation) {
    bool wasPreventingObserverSnapshot = this->preventsObserverSnapshot();
    this->shadowElevation = shadowElevation;
    // A shadow makes this node's drawing transform-dependent, which its observers read
    // when deciding whether they may snapshot, so a change to it has to reach them even
    // if they were already told stale this frame.
    if (this->preventsObserverSnapshot() != wasPreventingObserverSnapshot) {
        this->observersNotified = false;
    }
    // A shadow also lets the node draw outside its bounds, widening the cull rect.
    this->invalidateSnapshot(kContent);
}

void RenderNode::setAmbientShadowColor(SkColor ambientShadowColor) {
    this->ambientShadowColor = ambientShadowColor;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setSpotShadowColor(SkColor spotShadowColor) {
    this->spotShadowColor = spotShadowColor;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setRotationX(float rotationX) {
    this->rotationX = rotationX;
    this->matrixDirty = true;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setRotationY(float rotationY) {
    this->rotationY = rotationY;
    this->matrixDirty = true;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setRotationZ(float rotationZ) {
    this->rotationZ = rotationZ;
    this->matrixDirty = true;
    this->invalidateSnapshot(kAppearance);
}

float RenderNode::getCameraDistance() const {
    return this->transformCamera.fLocation.z / 72.0f;
}

void RenderNode::setCameraDistance(float cameraDistance) {
    this->setCameraLocation(0.0f, 0.0f, cameraDistance);
    this->matrixDirty = true;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setClipRect(const std::optional<SkRect>& clipRect, SkClipOp op, bool doAntiAlias) {
    this->clipRect = clipRect;
    this->clipRRect.reset();
    this->clipPath.reset();
    this->clipOp = op;
    this->clipAntiAlias = doAntiAlias;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setClipRRect(const std::optional<SkRRect>& clipRRect, SkClipOp op, bool doAntiAlias) {
    this->clipRect.reset();
    this->clipRRect = clipRRect;
    this->clipPath.reset();
    this->clipOp = op;
    this->clipAntiAlias = doAntiAlias;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setClipPath(const std::optional<SkPath>& clipPath, SkClipOp op, bool doAntiAlias) {
    this->clipRect.reset();
    this->clipRRect.reset();
    this->clipPath = clipPath;
    this->clipOp = op;
    this->clipAntiAlias = doAntiAlias;
    this->invalidateSnapshot(kAppearance);
}

void RenderNode::setClip(bool clip) {
    this->clip = clip;
    // Clipping bounds the recording's cull rect to the node's own bounds.
    this->invalidateSnapshot(kContent);
}

const SkMatrix& RenderNode::getMatrix() {
    this->updateMatrix();
    return this->transformMatrix;
}

bool RenderNode::mayDrawOutOfBounds() const {
    return !this->clip || this->shadowElevation > 0.0f;
}

// The content cache and the snapshot recorded from it have to agree on the cull rect
// and the bounding hierarchy, or the two cull differently and report different bounds.
// They cannot share a recorder: the snapshot is taken while the member one may be
// mid-recording.
SkCanvas *RenderNode::beginRecordingInto(SkPictureRecorder& target) {
    bool measureDrawBounds = this->mayDrawOutOfBounds();
    const SkRect& bounds = measureDrawBounds ? UNKNOWN_BOUNDS : SkRect::MakeWH(this->bounds.width(), this->bounds.height());
    SkBBHFactory* bbhFactory = measureDrawBounds ? this->bbhFactory : nullptr;
    return target.beginRecording(bounds, bbhFactory);
}

SkCanvas *RenderNode::beginRecording() {
    return this->beginRecordingInto(this->recorder);
}

void RenderNode::endRecording() {
    this->contentCache = this->recorder.finishRecordingAsDrawable();
    this->updateDependencies();
    this->invalidateSnapshot(kContent | kDependencies);
}

void RenderNode::drawInto(SkCanvas* canvas) {
    canvas->drawDrawable(this);
}

SkFlattenable::Type RenderNode::getFlattenableType() const {
    return kRenderNode_Type;
}

SkRect RenderNode::onGetBounds() {
    this->updateMatrix();
    SkRect result;
    if (this->contentCache) {
        result = this->contentCache->getBounds().makeOffset(this->bounds.left(), this->bounds.top());
    } else {
        result = this->mayDrawOutOfBounds() ? UNKNOWN_BOUNDS : this->bounds;
    }
    if (!this->matrixIdentity) {
        this->transformMatrix.mapRect(&result);
    }
    return result;
}

size_t RenderNode::onApproximateBytesUsed() {
    size_t contentSize = 0;
    if (this->contentCache) {
        contentSize += this->contentCache->approximateBytesUsed();
    }
    if (this->contentSnapshot) {
        // A snapshot references the snapshots nested in it rather than copying them, and
        // SkPicture already counts those, so the figure reported here covers the subtree
        // and must not be summed with the figures its dependencies report.
        contentSize += this->contentSnapshot->approximateBytesUsed();
    }
    if (this->contentLiveSnapshot) {
        // Likewise, apart from the nodes it references, which report their own.
        contentSize += this->contentLiveSnapshot->approximateBytesUsed();
    }
    if (this->clipPath) {
        contentSize += this->clipPath->approximateBytesUsed();
    }
    return sizeof(*this) + contentSize;
}


void RenderNode::onDraw(SkCanvas* canvas) {
    this->updateMatrix();
    // Whatever draws it next draws its current state, so its next change is news again.
    this->subtreeChangeRelayed = false;
    if (inlineDepth > 0) {
        // A snapshot recording inlines the node afresh, so its next change must be relayed
        // to the observers again. A live draw (including one through a reference) inlines
        // nothing.
        this->observersNotified = false;
        this->inlinedSinceNotify = true;
    }

    LayerBatch* batch = LayerBatch::get();
    if (batch) {
        if (batch->isCollecting(canvas)) {
            this->collectLayers(canvas, batch);
            return;
        }
    } else if (LayerBatch::shouldStart(canvas) && this->hasBatchableLayerInSubtree()) {
        LayerBatch session(canvas);
        session.collect(this);
        session.render();
        this->drawNode(canvas, &session);
        return;
    }
    this->drawNode(canvas, batch);
}

void RenderNode::drawNode(SkCanvas* canvas, LayerBatch* batch) {
    canvas->translate(this->bounds.left(), this->bounds.top());
    if (!this->matrixIdentity) {
        canvas->concat(this->transformMatrix);
    }

    if (this->shadowElevation > 0) {
        auto lightGeometry = this->context->getLightGeometry();
        auto lightInfo = this->context->getLightInfo();
        this->drawShadow(canvas, lightGeometry, lightInfo);
    }

    if (this->clip) {
        canvas->save();
        if (this->clipRect) {
            canvas->clipRect(*this->clipRect, this->clipOp, this->clipAntiAlias);
        } else if (this->clipRRect) {
            canvas->clipRRect(*this->clipRRect, this->clipOp, this->clipAntiAlias);
        } else if (this->clipPath) {
            canvas->clipPath(*this->clipPath, this->clipOp, this->clipAntiAlias);
        } else {
            auto rect = SkRect::MakeWH(this->bounds.width(), this->bounds.height());
            canvas->clipRect(rect, this->clipOp, this->clipAntiAlias);
        }
    }

    if (this->layerPaint) {
        auto rect = SkRect::MakeWH(this->bounds.width(), this->bounds.height());
        if (this->drawRetainedLayer(canvas, rect)) {
            return;
        }
        if (batch && this->layerBatchable && batch->composite(this, canvas, *this->layerPaint, rect)) {
            return;
        }
        canvas->saveLayer(rect, &*this->layerPaint);
    } else {
        canvas->save();
    }

    if (this->alpha < 1.0f && !this->layerPaint) {
        // Modulating alpha at replay time has to reach every recorded paint, which
        // means unrolling the content. A snapshot is only cheap while it is replayed
        // whole, so this path neither records nor uses one.
        AlphaModulateFilterCanvas alphaCanvas(canvas, this->alpha);
        DepthScope everything(inlineEverythingDepth);
        alphaCanvas.drawDrawable(this->contentCache.get());
    } else {
        this->drawLayerContent(canvas);
    }
}

void RenderNode::drawLayerContent(SkCanvas* canvas) {
    if (inlineEverythingDepth > 0) {
        // Through an alpha filter canvas, or into a picture of the node: as before live
        // snapshots, the picture snapshot (or the content where there is none).
        this->updateSnapshot();
        if (this->contentSnapshot) {
            canvas->drawPicture(this->contentSnapshot.get());
        } else {
            canvas->drawDrawable(this->contentCache.get());
        }
        return;
    }
    if (this->dependencies.empty() && isGpuCanvas(canvas)) {
        // Content that draws no other node replays the same from its recording as from a
        // snapshot of it, so a GPU canvas draws it as is. The snapshot exists for the
        // recordings that inline this node (they reference it instead of copying the
        // content), and is taken when one of them draws it.
        canvas->drawDrawable(this->contentCache.get());
        return;
    }
    this->updateLiveSnapshot();
    if (this->contentLiveSnapshot) {
        canvas->drawDrawable(this->contentLiveSnapshot.get());
    } else {
        canvas->drawDrawable(this->contentCache.get());
    }
}

// Mirrors drawNode on the collect pass's canvas, without drawing anything.
void RenderNode::collectLayers(SkCanvas* canvas, LayerBatch* batch) {
    canvas->translate(this->bounds.left(), this->bounds.top());
    if (!this->matrixIdentity) {
        canvas->concat(this->transformMatrix);
    }

    if (this->clip) {
        canvas->save();
        if (this->clipRect) {
            canvas->clipRect(*this->clipRect, this->clipOp, this->clipAntiAlias);
        } else if (this->clipRRect) {
            canvas->clipRRect(*this->clipRRect, this->clipOp, this->clipAntiAlias);
        } else if (this->clipPath) {
            canvas->clipPath(*this->clipPath, this->clipOp, this->clipAntiAlias);
        } else {
            auto rect = SkRect::MakeWH(this->bounds.width(), this->bounds.height());
            canvas->clipRect(rect, this->clipOp, this->clipAntiAlias);
        }
    }

    if (this->layerPaint) {
        // A layer nested in another is left to saveLayer, so the walk stops at the first.
        if (this->layerBatchable && !this->retainedLayerMatches(canvas)) {
            batch->request(this, canvas, SkRect::MakeWH(this->bounds.width(), this->bounds.height()));
        }
        return;
    }
    if (this->alpha < 1.0f) {
        // Drawn through an alpha filter canvas, which the atlas does not reach.
        return;
    }
    if (!this->hasBatchableLayerInSubtree()) {
        return;
    }
    this->drawLayerContent(canvas);
}

void RenderNode::relaySubtreeChange() {
    if (this->subtreeChangeRelayed) {
        return;
    }
    this->subtreeChangeRelayed = true;
    for (auto renderNode : this->observers) {
        renderNode->subtreeGeneration++;
        renderNode->relaySubtreeChange();
    }
}

void RenderNode::releaseRetainedLayer() {
    if (this->retainedLayer) {
        this->retainedLayer->release();
        this->retainedLayer->hasLastKey = false;
    }
}

bool RenderNode::retainedLayerMatches(SkCanvas* canvas) const {
    const RetainedLayer* retained = this->retainedLayer.get();
    return retained && retained->image && retained->key.content == this->subtreeGeneration &&
           sameMatrix(retained->key.ctm, canvas->getLocalToDevice());
}

bool RenderNode::drawRetainedLayer(SkCanvas* canvas, const SkRect& rect) {
    // Changes below a node only reach it through the observers, which are tracked with the
    // snapshot cache.
    if (!layerRetainingEnabled() || !this->context->shouldUseSnapshotCache() ||
        !canvas->recordingContext() || canvas->recorder()) {
        return false;
    }
    bool filtered = this->layerPaint->getImageFilter() != nullptr;
    if (!this->layerBatchable) {
        // An image filter layer whose restore is otherwise a plain composite.
        SkPaint unfiltered(*this->layerPaint);
        unfiltered.setImageFilter(nullptr);
        if (!filtered || !isBatchableLayerPaint(unfiltered)) {
            return false;
        }
    }
    SkDevice* device = SkCanvasPriv::TopDevice(canvas);
    if (!device || !(device->deviceToGlobal() == SkM44()) || device->isClipEmpty()) {
        return false;
    }
    drainGraveyard();

    RetainedLayerKey key;
    key.context = canvas->recordingContext();
    key.ctm = device->localToDevice44();
    if (filtered) {
        SkM44 inverse;
        if (!key.ctm.isFinite() || !key.ctm.invert(&inverse)) {
            return false;
        }
        key.bounds = device->devClipBounds();
    } else if (!layerContentBounds(key.ctm, rect, &key.bounds) || !key.bounds.intersect(device->devClipBounds())) {
        return false;
    }
    key.content = this->subtreeGeneration;
    key.paint = filtered ? this->layerPaintGeneration : 0;
    key.lighting = this->context->getLightingGeneration();
    SkImageInfo targetInfo = canvas->imageInfo();
    key.colorType = targetInfo.colorType();
    key.colorSpace = targetInfo.colorSpace();

    if (!this->retainedLayer) {
        this->retainedLayer = std::make_unique<RetainedLayer>();
    }
    RetainedLayer* retained = this->retainedLayer.get();
    if (!(retained->image && retained->key == key)) {
        if (retained->image) {
            retained->churn = retained->hits < kRetainedLayerPayback
                ? std::min(retained->churn + 1, kRetainedLayerMaxChurn)
                : std::max(retained->churn - 1, 0);
        }
        retained->release();
        retained->hits = 0;
        if (retained->hasLastKey && retained->lastKey == key) {
            retained->stableDraws++;
        } else {
            retained->stableDraws = 1;
        }
        retained->lastKey = key;
        retained->hasLastKey = true;
        if (retained->stableDraws < (kRetainedLayerStableDraws << retained->churn)) {
            return false;
        }

        // The same layer on enough draws in a row: render it once, and keep it.
        SkIRect area = filtered ? SkIRect::MakeLTRB(0, 0, key.bounds.right(), key.bounds.bottom()) : key.bounds;
        if (area.width() <= 0 || area.height() <= 0 || area.width() > 8192 || area.height() > 8192) {
            return false;
        }
        SkColorType colorType = key.colorType;
        if (SkColorTypeBytesPerPixel(colorType) <= 4 &&
            colorType != kRGBA_8888_SkColorType && colorType != kBGRA_8888_SkColorType) {
            colorType = kN32_SkColorType;
        }
        SkImageInfo info = SkImageInfo::Make(area.width(), area.height(), colorType, kPremul_SkAlphaType,
                                             targetInfo.refColorSpace());
        size_t bytes = info.computeMinByteSize();
        if (retainedLayerBytes.load(std::memory_order_relaxed) + bytes > layerRetainingBudget()) {
            return false;
        }
        SkSurfaceProps props = canvas->getTopProps().cloneWithPixelGeometry(kUnknown_SkPixelGeometry);
        sk_sp<SkSurface> surface = canvas->makeSurface(info, &props);
        if (!surface) {
            return false;
        }
        SkCanvas* layerCanvas = surface->getCanvas();
        layerCanvas->clear(SK_ColorTRANSPARENT);
        if (filtered) {
            // The filtered layer as saveLayer makes it: the same device space and clip bounds,
            // composited into the texture as is, so the texture holds the filter's output.
            layerCanvas->clipRect(SkRect::Make(key.bounds));
            layerCanvas->setMatrix(key.ctm);
            SkPaint filterPaint;
            filterPaint.setImageFilter(this->layerPaint->refImageFilter());
            layerCanvas->saveLayer(rect, &filterPaint);
            this->drawLayerContent(layerCanvas);
            layerCanvas->restore();
            retained->origin = { 0, 0 };
        } else {
            // The layer's device, as SkCanvas::internalSaveLayer sets it up.
            layerCanvas->setMatrix(SkM44::Translate(static_cast<float>(-key.bounds.left()),
                                                    static_cast<float>(-key.bounds.top())) * key.ctm);
            this->drawLayerContent(layerCanvas);
            retained->origin = { key.bounds.left(), key.bounds.top() };
        }
        retained->image = surface->makeTemporaryImage();
        if (!retained->image) {
            return false;
        }
        retained->surface = std::move(surface);
        retained->key = key;
        retained->bytes = bytes;
        retained->thread = std::this_thread::get_id();
        retainedLayerBytes.fetch_add(bytes, std::memory_order_relaxed);
        if (layerBatchingLog()) {
            fprintf(stderr, "[skiko] retained layer %dx%d%s, %zu KB retained in all\n", area.width(), area.height(),
                    filtered ? " (filtered)" : "", retainedLayerBytes.load() >> 10);
        }
    }

    retained->hits++;
    SkPaint paint = layerRestorePaint(*this->layerPaint);
    SkIRect bounds = key.bounds;
    if (filtered) {
        // Outside the filter's output the texture is transparent, which the paint leaves as is.
        SkRect output = this->layerPaint->getImageFilter()->computeFastBounds(rect);
        SkIRect outputBounds = key.ctm.asM33().mapRect(output).roundOut();
        outputBounds.outset(1, 1);
        if (!bounds.intersect(outputBounds)) {
            return true;
        }
    }
    SkRect src = SkRect::Make(bounds.makeOffset(-retained->origin.x(), -retained->origin.y()));
    canvas->save();
    canvas->resetMatrix();
    canvas->drawImageRect(retained->image.get(), src, SkRect::Make(bounds), SkSamplingOptions(), &paint,
                          SkCanvas::kFast_SrcRectConstraint);
    canvas->restore();
    return true;
}

bool RenderNode::hasBatchableLayerInSubtree() {
    // Without the snapshot cache the nodes a node draws are not tracked.
    if (!this->context->shouldUseSnapshotCache()) {
        return true;
    }
    if (this->layerInfoDirty) {
        bool has = this->layerBatchable;
        if (!this->layerPaint) {
            // Every dependency is computed (no early exit), so that a clean node only ever
            // read clean dependencies, which keeps markLayerInfoDirty's early exit valid.
            for (auto renderNode : this->dependencies) {
                if (renderNode->hasBatchableLayerInSubtree()) {
                    has = true;
                }
            }
        }
        this->subtreeHasBatchableLayer = has;
        this->layerInfoDirty = false;
    }
    return this->subtreeHasBatchableLayer;
}

void RenderNode::markLayerInfoDirty() {
    if (this->layerInfoDirty) {
        return;
    }
    this->layerInfoDirty = true;
    for (auto renderNode : this->observers) {
        renderNode->markLayerInfoDirty();
    }
}

// The shadow is drawn against the context's light geometry, which is expressed in the
// coordinate space the resulting picture is replayed in.
sk_sp<SkPicture> RenderNode::onMakePictureSnapshot() {
    SkCanvas* canvas = this->beginRecording();
    DepthScope everything(inlineEverythingDepth);
    UnrollDrawableCanvas unrollCanvas(canvas);
    this->draw(&unrollCanvas);
    return this->recorder.finishRecordingAsPicture();
}

// Adoption from frameworks/base/libs/hwui/RenderProperties.cpp
void RenderNode::updateMatrix() {
    if (!this->matrixDirty) {
        return;
    }
    float pivotX, pivotY;
    if (this->pivot.isFinite()) {
        pivotX = this->pivot.fX;
        pivotY = this->pivot.fY;
    } else {
        pivotX = this->bounds.width() / 2.0f;
        pivotY = this->bounds.height() / 2.0f;
    }
    this->transformMatrix.reset();
    if (isZero(this->rotationX) && isZero(this->rotationY)) {
        this->transformMatrix.setTranslate(this->translationX, this->translationY);
        this->transformMatrix.preRotate(this->rotationZ, pivotX, pivotY);
        this->transformMatrix.preScale(this->scaleX, this->scaleY, pivotX, pivotY);
    } else {
        this->transformMatrix.preScale(this->scaleX, this->scaleY, pivotX, pivotY);

        SkM44 transform3D;
        transform3D.preConcat(SkM44::Rotate({1, 0, 0}, -this->rotationX * SK_ScalarPI / 180));
        transform3D.preConcat(SkM44::Rotate({0,-1, 0}, -this->rotationY * SK_ScalarPI / 180));
        transform3D.preConcat(SkM44::Rotate({0, 0, 1}, -this->rotationZ * SK_ScalarPI / 180));
        SkPatch3D patch3D;
        patch3D.transform(transform3D);

        SkMatrix transform;
        this->transformCamera.patchToMatrix(patch3D, &transform);
        transform.preTranslate(-pivotX, -pivotY);
        transform.postTranslate(pivotX + this->translationX, pivotY + this->translationY);
        this->transformMatrix.postConcat(transform);
    }
    this->matrixDirty = false;
    this->matrixIdentity = this->transformMatrix.isIdentity();
}

// Maintains the invariant `node in dep->observers` iff `dep in node->dependencies`.
// Only dependencies holds references: an observer registration is a bare back
// pointer, so that a parent drawing a child does not form a reference cycle.
void RenderNode::releaseDependencies() {
    for (auto renderNode : this->dependencies) {
        renderNode->observers.erase(this);
        renderNode->unref();
    }
    this->dependencies.clear();
}

void RenderNode::updateDependencies() {
    // Tracking exists to invalidate the snapshots that inline this node, so it is only
    // worth its replay of the recording where snapshots are taken at all.
    if (!this->context->shouldUseSnapshotCache()) {
        return;
    }
    this->releaseDependencies();
    if (this->contentCache) {
        DependencyTrackerCanvas dependencyTracker(this->dependencies);
        this->contentCache->draw(&dependencyTracker);

        for (auto renderNode : this->dependencies) {
            renderNode->observers.insert(this);
        }
    }
    this->layerInfoDirty = false;
    this->markLayerInfoDirty();
}

void RenderNode::updateSnapshot() {
    if (!this->context->shouldUseSnapshotCache() || !this->contentTransformInvariant ||
        !this->contentCache || this->contentSnapshot) {
        return;
    }

    SkPictureRecorder snapshotRecorder;
    SkCanvas* recordingCanvas = this->beginRecordingInto(snapshotRecorder);
    // Force unrolling all drawables to avoid nested snapshotting.
    DepthScope inlining(inlineDepth);
    DepthScope everything(inlineEverythingDepth);
    UnrollDrawableCanvas unrollCanvas(recordingCanvas);
    this->contentCache->draw(&unrollCanvas);
    this->contentSnapshot = snapshotRecorder.finishRecordingAsPicture();
}

void RenderNode::updateLiveSnapshot() {
    if (!this->context->shouldUseSnapshotCache() || !this->contentCache || this->contentLiveSnapshot) {
        return;
    }
    SkPictureRecorder snapshotRecorder;
    SkCanvas* recordingCanvas = this->beginRecordingInto(snapshotRecorder);
    SkRect cullRect = this->mayDrawOutOfBounds() ? UNKNOWN_BOUNDS : SkRect::MakeWH(this->bounds.width(), this->bounds.height());
    {
        DepthScope inlining(inlineDepth);
        UnrollDrawableCanvas unrollCanvas(recordingCanvas, /*referencing=*/true);
        this->contentCache->draw(&unrollCanvas);
    }
    this->contentLiveSnapshot = sk_make_sp<ContentSnapshot>(snapshotRecorder.finishRecordingAsDrawable(), cullRect);
}

bool RenderNode::shouldBeReferenced() const {
    return this->volatileNode ||
           this->shadowElevation > 0.0f ||
           (this->layerBatchable && layerBatchingEnabled()) ||
           (this->layerPaint && layerRetainingEnabled()) ||
           (this->alpha < 1.0f && !this->layerPaint && !this->dependencies.empty());
}

bool RenderNode::drawsTransformDependentContent() const {
    return this->shadowElevation > 0.0f;
}

bool RenderNode::preventsObserverSnapshot() const {
    return this->drawsTransformDependentContent() || !this->contentTransformInvariant;
}

// Snapshots hold the recorded content only: the transform, clip, layer, alpha and
// shadow are applied around them in onDraw, so which of those changed decides whether
// this node's own snapshot survives. None of them leaves the snapshots that inlined
// this node's drawing valid, so those are dropped whatever the change was.
void RenderNode::invalidateSnapshot(uint32_t changes) {
    this->notifyDrawingChanged();
    if (changes & kContent) {
        this->subtreeGeneration++;
    }
    this->relaySubtreeChange();

    if (changes & kDependencies) {
        bool wasPreventingObserverSnapshot = this->preventsObserverSnapshot();
        this->contentTransformInvariant = true;
        for (auto renderNode : this->dependencies) {
            if (renderNode->preventsObserverSnapshot()) {
                this->contentTransformInvariant = false;
                break;
            }
        }
        // Observers derive their own snapshottability from this bit, so a change to it has
        // to reach them even if they were already told stale this frame.
        if (this->preventsObserverSnapshot() != wasPreventingObserverSnapshot) {
            this->observersNotified = false;
        }
    }
    if (changes & kContent) {
        this->contentSnapshot.reset();
        this->contentLiveSnapshot.reset();
    }

    // Observers inlined this node's drawing when they recorded, so they hold a stale copy
    // until they record again -- which redraws this node and clears the flag below. One
    // notification per draw is enough to invalidate them, and telling them once instead of
    // once per observer path bounds the walk when a node is drawn through several of them.
    if (!this->observersNotified) {
        this->observersNotified = true;
        if (this->inlinedSinceNotify) {
            this->inlinedSinceNotify = false;
            if (this->changesAfterInline < 2 && ++this->changesAfterInline == 2) {
                // Changing again after the observers inlined it anew: from now on they
                // reference it instead, and its changes stop reaching them.
                this->volatileNode = true;
            }
        }
        for (auto renderNode : this->observers) {
            renderNode->invalidateSnapshot(kContent | kDependencies);
        }
    }
}

void RenderNode::drawShadow(SkCanvas *canvas, const LightGeometry& lightGeometry, const LightInfo& lightInfo) {
    SkPath tmpPath, *path = &tmpPath;
    if (this->clipRect) {
        tmpPath = SkPathBuilder().addRect(*this->clipRect).detach();
    } else if (this->clipRRect) {
        tmpPath = SkPathBuilder().addRRect(*this->clipRRect).detach();
    } else if (this->clipPath) {
        path = &*this->clipPath;
    } else {
        return;
    }

    SkPoint3 zParams{0.0f, 0.0f, this->shadowElevation};
    float ambientAlpha = lightInfo.ambientShadowAlpha * this->alpha;
    float spotAlpha = lightInfo.spotShadowAlpha * this->alpha;
    SkColor ambientColor = multiplyAlpha(this->ambientShadowColor, ambientAlpha);
    SkColor spotColor = multiplyAlpha(this->spotShadowColor, spotAlpha);

    SkShadowUtils::DrawShadow(
        canvas,
        *path,
        zParams,
        lightGeometry.center,
        lightGeometry.radius,
        ambientColor,
        spotColor,
        this->alpha < 1.0f ? SkShadowFlags::kTransparentOccluder_ShadowFlag : SkShadowFlags::kNone_ShadowFlag
    );
}

// Adoption from frameworks/base/libs/hwui/RenderProperties.cpp
void RenderNode::setCameraLocation(float x, float y, float z) {
    // the camera location is passed in inches, set in pt
    SkScalar lz = z * 72.0f;
    this->transformCamera.fLocation = {x * 72.0f, y * 72.0f, lz};
    this->transformCamera.fObserver = {0, 0, lz};
    this->transformCamera.update();
}

} // namespace node
} // namespace skiko
