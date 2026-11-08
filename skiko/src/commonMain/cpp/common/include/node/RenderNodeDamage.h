#pragma once
#include <cstdint>
#include <vector>
#include <SkMatrix.h>
#include <SkRect.h>
#include <SkRefCnt.h>
#include <SkRegion.h>

namespace skiko {
namespace node {

class RenderNode;

// Damage tracking: which device pixels a frame of render nodes changes since the previous frame
// drawn through the same tracker, so that a host keeping the previous frame's pixels redraws only
// those (clipped to them) and copies the rest.
//
// For each frame the host calls beginFrame, then addNode for every root node the frame draws, in
// drawing order and at the transform it draws them, and endFrame, after which isFull and getRects
// tell the damage. The nodes are walked as they will be drawn: each node's damage is the device
// area its drawing covers (its subtree's), before and after, where its own state, its transform or
// its clip changed; a subtree nothing changed in is not walked again. Damage covers a layer whole
// or not at all, so that the layer is rendered at the same bounds (and to the same pixels) as in a
// full redraw. The whole target is damaged on the first frame, after a size change or invalidate(),
// and where the nodes draw something whose changes can't be tracked (a backdrop layer).
//
// The nodes have to be created with a context whose damage tracking is on (it measures what their
// recordings draw), and the snapshot cache on (it relays changes upwards). A node's drawing is
// skipped where a walk found it outside the canvas's clip.
class RenderNodeDamageTracker : public SkRefCnt {
public:
    RenderNodeDamageTracker();
    ~RenderNodeDamageTracker() override;

    void beginFrame(int width, int height);
    void addNode(RenderNode *node, const SkMatrix &matrix);
    // Damages the whole target in the next frame (content drawn besides the nodes changed).
    void invalidate() { this->fullNext = true; }
    // Whether recordings measure what they draw for it (on from the start). Off, a host drawing whole
    // frames for a while saves the measuring; what is recorded meanwhile counts as drawing the clip.
    void setMeasuring(bool measuring);
    void endFrame();

    bool isFull() const { return this->full; }
    // The damaged rects (none when nothing changed); not overlapping, within the target.
    const std::vector<SkIRect> &getRects() const { return this->rects; }

    // What the last walk did, for logs.
    int walkedNodes() const { return this->walked; }
    int skippedNodes() const { return this->skipped; }

private:
    friend class DamageWalk;

    uint32_t id;
    uint32_t frame = 0;
    int width = -1, height = -1;
    bool fullNext = true;
    bool measuring = true;
    bool full = true;
    int walked = 0, skipped = 0;
    std::vector<sk_sp<RenderNode>> roots, previousRoots;
    std::vector<SkIRect> rootBounds, previousRootBounds;
    std::vector<SkIRect> layers;
    SkRegion region;
    std::vector<SkIRect> rects;
};

} // namespace node
} // namespace skiko
