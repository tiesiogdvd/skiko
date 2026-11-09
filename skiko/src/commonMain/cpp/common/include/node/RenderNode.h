#pragma once
#include <memory>
#include <optional>
#include <unordered_set>
#include <vector>
#include <SkM44.h>
#include <SkBBHFactory.h>
#include <SkCamera.h>
#include <SkCanvas.h>
#include <SkColor.h>
#include <SkDrawable.h>
#include <SkMatrix.h>
#include <SkPaint.h>
#include <SkPath.h>
#include <SkPictureRecorder.h>
#include <SkPoint.h>
#include <SkRRect.h>
#include <SkRect.h>
#include <SkRefCnt.h>
#include "Lighting.h"
#include "RenderNodeRasterCache.h"

namespace skiko {
namespace node {

class RenderNodeContext;
class LayerBatch;
class DamageWalk;
class RenderNode;
class OwnBoundsHierarchy;
class RenderNodeTwins;
struct TwinState;

// What a damage walk (RenderNodeDamageTracker) last found about a node: the state it was drawn
// in, and the device area its drawing covered.
struct DamageRecord {
    // The tracker and frame of the walk that made the record; 0 for none.
    uint32_t tracker = 0;
    uint32_t frame = 0;
    uint32_t ownGeneration = 0;
    uint32_t propertyGeneration = 0;
    uint32_t subtreeGeneration = 0;
    uint32_t lighting = 0;
    // Where the node was drawn: its device transform and the device clip bounds before its own clip.
    SkM44 ctm;
    SkIRect clip = SkIRect::MakeEmpty();
    // The device bounds of everything the node draws, its subtree included.
    SkIRect bounds = SkIRect::MakeEmpty();
    // The bounds before the last walk that changed them (see multiple).
    SkIRect previousBounds = SkIRect::MakeEmpty();
    // Whether the node was drawn more than once in the walk's frame; then the bounds cover every
    // occurrence, and the node counts as changed in the next frame.
    bool multiple = false;
    // The device bounds of the layers drawn in the subtree, which damage covers whole or not at all.
    std::vector<SkIRect> layers;
    // Set where the node was drawn anew by a recording that changed around it (added, moved
    // between other drawing): the next walk damages it whole.
    bool forced = false;
};

// One operation of a recording, as damage tracking compares two recordings of a node: a hash of
// the operation and the state it draws in, and its bounds in the recording's space (unbounded
// where unknown); for a drawn node, the node.
struct ContentOp {
    uint64_t hash;
    SkRect bounds;
    bool bounded;
    RenderNode* node;
};

class RenderNode : public SkDrawable {
public:
    RenderNode(const sk_sp<RenderNodeContext>& context);
    ~RenderNode();

    std::optional<SkPaint>& getLayerPaint() { return this->layerPaint; }
    void setLayerPaint(const std::optional<SkPaint>& layerPaint);
    const SkRect& getBounds() const { return this->bounds; }
    void setBounds(const SkRect& bounds);
    const SkPoint& getPivot() const { return this->pivot; }
    void setPivot(const SkPoint& pivot);
    float getAlpha() const { return this->alpha; }
    void setAlpha(float alpha);
    float getScaleX() const { return this->scaleX; }
    void setScaleX(float scaleX);
    float getScaleY() const { return this->scaleY; }
    void setScaleY(float scaleY);
    float getTranslationX() const { return this->translationX; }
    void setTranslationX(float translationX);
    float getTranslationY() const { return this->translationY; }
    void setTranslationY(float translationY);
    float getShadowElevation() const { return this->shadowElevation; }
    void setShadowElevation(float shadowElevation);
    SkColor getAmbientShadowColor() const { return this->ambientShadowColor; }
    void setAmbientShadowColor(SkColor ambientShadowColor);
    SkColor getSpotShadowColor() const { return this->spotShadowColor; }
    void setSpotShadowColor(SkColor spotShadowColor);
    float getRotationX() const { return this->rotationX; }
    void setRotationX(float rotationX);
    float getRotationY() const { return this->rotationY; }
    void setRotationY(float rotationY);
    float getRotationZ() const { return this->rotationZ; }
    void setRotationZ(float rotationZ);
    float getCameraDistance() const;
    void setCameraDistance(float cameraDistance);
    void setClipRect(const std::optional<SkRect>& clipRect, SkClipOp op = SkClipOp::kIntersect, bool doAntiAlias = false);
    void setClipRRect(const std::optional<SkRRect>& clipRRect, SkClipOp op = SkClipOp::kIntersect, bool doAntiAlias = false);
    void setClipPath(const std::optional<SkPath>& clipPath, SkClipOp op = SkClipOp::kIntersect, bool doAntiAlias = false);
    bool getClip() const { return this->clip; }
    void setClip(bool clip);

    const SkMatrix& getMatrix();

    SkCanvas * beginRecording();
    void endRecording();

    void drawInto(SkCanvas *canvas);

    SkFlattenable::Type getFlattenableType() const override;

    // Whether this node's layer can be drawn from a frame's layer atlas instead of a
    // layer of its own: a layer paint whose restore is a plain composite of the layer's
    // pixels (no image filter, and nothing that touches pixels outside the content).
    bool isBatchableLayer() const { return this->layerBatchable; }
    // Draws what this node draws inside its layer: the recorded content.
    void drawLayerContent(SkCanvas *canvas);
    // Whether a snapshot recording that reaches this node records a reference to it, drawn
    // live when the snapshot is replayed, instead of inlining its drawing. True for a node
    // whose drawing changes after it was inlined (an animated node), one whose drawing
    // depends on where it is replayed (a shadow), one the layer atlas draws, and one drawing
    // other nodes through an alpha filter.
    bool shouldBeReferenced() const;

    // The damage walk's visit of this node (see RenderNodeDamageTracker).
    void walkDamage(SkCanvas *canvas, DamageWalk *walk);

    // The properties a RenderNodeAnimationClock animates (a bit per AnimatedProperty), set by the clock. An animating
    // node is referenced from the snapshots that reach it, and while only its alpha animates its layer keeps its
    // texture from the first frame (the alpha is applied as the texture is composited); while its transform animates
    // it may be composited from a promoted texture (SKIKO_RENDER_NODE_PROMOTE=1), and the end of that motion counts
    // as a change, so that it is drawn exactly again (see RenderNodeRasterCache.h).
    uint32_t getAnimatingProperties() const { return this->animatingProperties; }
    void setAnimatingProperties(uint32_t properties);

    // This node's twin for twins, brought up to date with this node and the nodes it draws (see
    // RenderNodeTwins). The nodes' thread, while nothing draws the twins.
    RenderNode* syncTwin(RenderNodeTwins *twins);

    static bool isRenderNode(SkDrawable* drawable);
    // A frame starts after this node composited a promoted texture inexactly: if nothing about it
    // changed since, its animation ended, and it is marked changed to be drawn exactly (see
    // RasterCache::beginFrame).
    void settle();

protected:
    // SkDrawable
    SkRect onGetBounds() override;
    size_t onApproximateBytesUsed() override;
    void onDraw(SkCanvas* canvas) override;
    sk_sp<SkPicture> onMakePictureSnapshot() override;

private:
    // What changed about a node, as far as snapshot bookkeeping is concerned.
    enum SnapshotChange : uint32_t {
        // Drawing applied around the snapshot: transform, clip, layer, alpha. This
        // node's own snapshot stays valid; the snapshots that inlined it do not.
        kAppearance = 1 << 0,
        // The recorded content, or the cull rect it is recorded with.
        kContent = 1 << 1,
        // The set of drawn nodes, or what they draw, may differ, so re-derive from
        // the current dependencies whether this content can be snapshotted.
        kDependencies = 1 << 2,
    };

    SkCanvas *beginRecordingInto(SkPictureRecorder& target);
    // Whether the content may cover more than the node's own bounds, and so has to be
    // measured while recording instead of being culled to them.
    bool mayDrawOutOfBounds() const;
    // Whether this node's own drawing only replays correctly at the transform it was
    // recorded under, which bars every node that inlines it from snapshotting too.
    bool drawsTransformDependentContent() const;
    // Whether an observer that inlines this node cannot snapshot the result: true when this
    // node draws a shadow, or when anything in its own content does. This is the single bit
    // an observer reads from each of its dependencies.
    bool preventsObserverSnapshot() const;
    void updateMatrix();
    void updateDependencies();
    void releaseDependencies();
    void updateSnapshot();
    void updateLiveSnapshot();
    void invalidateSnapshot(uint32_t changes);
    void drawShadow(SkCanvas *canvas, const LightGeometry& lightGeometry, const LightInfo& lightInfo);
    // The node's drawing onto a canvas, with [batch] being the frame's layer atlas, if any.
    void drawNode(SkCanvas *canvas, LayerBatch *batch);
    // The texture the node keeps between frames (RenderNodeRasterCache.cpp). drawFromCache draws
    // its layer (or, without one, its clip and content) from it where the frame's decision says
    // so, restCtm being where the node draws without its own transform; cacheWillDraw is the
    // decision as the atlas's collect pass sees it, target the canvas the frame draws onto.
    bool drawFromCache(SkCanvas *canvas, const SkM44& restCtm);
    bool cacheWillDraw(SkCanvas *canvas, SkCanvas *target, const SkM44& restCtm);
    NodeCache* cacheFor(SkCanvas *canvas, SkCanvas *target, const TextureOwner& owner);
    NodeCache::Decision cacheDecision(SkCanvas *canvas, SkCanvas *target, const SkM44& restCtm, const TextureOwner& owner);
    void releaseCache();
    void analyseSubtree();
    bool contentClipBounds(SkRect* local) const;
    bool contentExtentKnown(const SkM44& ctm) const;
    bool filteredLayerBounds(SkCanvas *canvas, const SkM44& ctm, SkIRect* result) const;
    void applyClip(SkCanvas *canvas) const;
    // Whether the raster cache may keep this node's content (it has to be drawn as a node, not
    // inlined into a snapshot, for that): no layer, full alpha, a clip or measured bounds, and
    // content worth it that composites the same through a texture.
    bool mayCacheContent();
    // Compares the recording about to replace the content with it, adding what changed to the
    // damage the next walk finds (see contentDamage).
    void diffContent(SkDrawable* recording);
    // Tells the observers that what they draw of this node changed (see subtreeGeneration).
    void relaySubtreeChange();
    // The layer atlas's first pass: finds the layers this node's subtree will draw and where.
    void collectLayers(SkCanvas *canvas, LayerBatch *batch);
    // Whether this node or a node it draws (outside any layer) has a batchable layer.
    bool hasBatchableLayerInSubtree();
    void markLayerInfoDirty();
    void setCameraLocation(float x, float y, float z);
    // A change to this node's own state: counted for damage tracking, then invalidates the snapshots.
    void changed(uint32_t changes);
    // Whether a damage walk found that this node, drawn where canvas now is, draws nothing inside
    // canvas's clip, so that drawing it can be skipped.
    bool rejectedByDamage(SkCanvas *canvas) const;
    // Render twins (see RenderNodeTwins): this node's properties onto its twin, its content (with the
    // nodes it draws replaced by their twins), and the twin's side: content made for it, drawing
    // dependencies (referenced already).
    void copyPropertiesTo(RenderNode *twin);
    void syncTwinContent(RenderNode *twin, RenderNodeTwins *twins);
    void setTwinContent(sk_sp<SkDrawable> content, std::unordered_set<RenderNode *>&& twinDependencies);
    void copyDamageRecordTo(RenderNode *twin, RenderNodeContext *twinContext) const;

    sk_sp<RenderNodeContext> context;

    // Changes whenever the recorded content does (see RenderNodeTwins).
    uint32_t contentGeneration;
    // This node's twin and what it was last synced from; null before the first sync.
    std::unique_ptr<TwinState> twinState;

    // Changes whenever this node's own state does (properties, layer paint, recorded content), and
    // whenever its properties or layer paint do.
    uint32_t ownGeneration;
    uint32_t propertyGeneration;
    // The device-independent bounds of what the recorded content draws itself, the nodes it draws
    // excluded (they report their own); unknown where the recording did not measure them.
    SkRect ownContentBounds;
    bool ownContentBoundsKnown;
    // The recording-depth marker of the content recording in progress (see beginRecording).
    int recordingDepth;
    sk_sp<OwnBoundsHierarchy> ownBoundsHierarchy;
    DamageRecord damage;
    // The operations of the recorded content, and what the recordings since the last damage walk
    // changed: the union of the changed operations' bounds (in the node's own space; unknown where
    // one is unbounded or there was nothing to compare with), and the device bounds a walk found
    // for the nodes no longer drawn where they were (per tracker).
    std::vector<ContentOp> contentOps;
    bool contentOpsKnown;
    SkRect contentDamage;
    bool contentDamageUnknown;
    std::vector<std::pair<uint32_t, SkIRect>> removedNodeDamage;

    SkBBHFactory *bbhFactory;
    SkPictureRecorder recorder;
    sk_sp<SkDrawable> contentCache;
    // The content with every node it draws inlined; only drawn where the content is replayed
    // through an alpha filter canvas or into a picture of the node, where references would
    // not see the filter or would freeze anyway.
    sk_sp<SkPicture> contentSnapshot;
    // The content with the nodes it draws inlined, except the ones that should be
    // referenced (see shouldBeReferenced), which it draws live.
    sk_sp<SkDrawable> contentLiveSnapshot;
    // Whether the recorded content replays correctly under any transform, and so may be
    // snapshotted at all. Cleared by drawing whose result depends on the transform it is
    // replayed under -- today only a shadow, whose spot geometry follows the occluder's
    // position relative to a light fixed in device space, so replaying the recording
    // anywhere else yields a different shadow.
    bool contentTransformInvariant;
    // Whether the observers have already been told, since this node was last drawn, that
    // their inlined copy of it is stale. Lets a node reached through several observer paths
    // propagate once instead of once per path. Cleared when the node is drawn (they inline
    // it afresh) and when preventsObserverSnapshot() flips (they must re-derive).
    bool observersNotified;
    // Whether a snapshot recording inlined this node since its observers were last told
    // of a change, and how many changes followed such an inlining: two make the node
    // volatile, and from then on referenced rather than inlined.
    bool inlinedSinceNotify;
    uint8_t changesAfterInline;
    bool volatileNode;

    std::unordered_set<RenderNode *> dependencies, observers;

    std::optional<SkPaint> layerPaint;
    bool layerBatchable;
    // Changes when the layer paint changes in more than its alpha, color filter or blend
    // mode (which a retained layer applies when compositing).
    uint32_t layerPaintGeneration;
    // Changes whenever what this node draws inside its layer changes: its own content, or
    // anything about a node it draws (relayed through the observers).
    uint32_t subtreeGeneration;
    // Whether the observers were told of a subtree change since this node was last drawn.
    bool subtreeChangeRelayed;
    std::unique_ptr<NodeCache> cache;
    // Cached hasBatchableLayerInSubtree(), valid unless layerInfoDirty. A dirty node's
    // observers are dirty too (see markLayerInfoDirty).
    bool layerInfoDirty;
    bool subtreeHasBatchableLayer;
    SkRect bounds;
    SkPoint pivot;
    float alpha;
    float scaleX, scaleY;
    float translationX, translationY;
    float shadowElevation;
    SkColor ambientShadowColor;
    SkColor spotShadowColor;
    float rotationX, rotationY, rotationZ;
    std::optional<SkRect> clipRect;
    std::optional<SkRRect> clipRRect;
    std::optional<SkPath> clipPath;
    SkClipOp clipOp;
    bool clipAntiAlias;
    bool clip;

    SkMatrix transformMatrix;
    SkCamera3D transformCamera;
    bool matrixIdentity;
    bool matrixDirty;

    uint32_t animatingProperties = 0;
};

} // namespace node
} // namespace skiko
