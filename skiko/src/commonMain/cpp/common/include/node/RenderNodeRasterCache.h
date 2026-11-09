#pragma once
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <thread>
#include <SkCanvas.h>
#include <SkImage.h>
#include <SkM44.h>
#include <SkPaint.h>
#include <SkRect.h>
#include <SkRefCnt.h>

namespace skiko {
namespace node {

class RenderNode;

// ---------------------------------------------------------------------------------------------
// The textures render nodes keep between frames.
//
// A node keeps at most one texture: its layer (a retained layer: a layer whose content and
// placement stay the same is rendered once and only composited after, as Android keeps a
// promoted layer), its content (the raster cache: a subtree drawn unchanged for a few frames is
// rasterised once and drawn as one image, Flutter's model), or either while it animates (a
// promotion: a subtree whose transform changes from frame to frame while its content doesn't is
// rendered once at a fixed raster transform and composited at each frame's transform, as Android
// keeps a hardware layer and Chrome a fixed raster scale for an animation's length).
//
// A texture belongs to the GPU context (Ganesh) or recorder (Graphite) it was made with, its
// owner, and is only drawn onto canvases of that owner. A Graphite texture is kept as an image
// alone (its surface is let go at once, which makes it immutable and leaves it no pointer to the
// recorder), and a recorder going away releases the textures made with it (see
// onGraphiteRecorderClosing); a recorder made later at the same address is another owner. A
// Ganesh image holds its context. Textures are released on the thread that made them (one
// released elsewhere is handed over and released there on its next frame or draw).
//
// All textures share one budget (SKIKO_RENDER_NODE_LAYER_RETAIN_MB, 96 MB by default); a texture
// not drawn for a while (SKIKO_RENDER_NODE_CACHE_MAX_AGE_MS, 5 s by default) is freed, and a host
// can free the ones not drawn lately (purge) and read how much they hold (bytes).
//
// Frames: a host that calls beginFrame at the start of each frame tells the caches what a frame
// is (several draws of a tree in one frame, a pass a damaged rect, count once), and in return
// promises a settle frame: after a frame that drew a promoted texture at a transform it doesn't
// reproduce exactly (settleRequested), the host draws one more frame, in which every node whose
// animation ended draws exactly again (and damages its bounds for that). Promotion is only used
// while a host does so; without it a frame is each top-level draw of a node.
// ---------------------------------------------------------------------------------------------

// Who a texture belongs to: a Ganesh context or a Graphite recorder, and, for a recorder, which
// one of those made at its address.
struct TextureOwner {
    const void* object = nullptr;
    uint64_t generation = 0;
    bool graphite = false;

    explicit operator bool() const { return this->object != nullptr; }
    bool operator==(const TextureOwner& other) const {
        return this->object == other.object && this->generation == other.generation;
    }
    bool operator!=(const TextureOwner& other) const { return !(*this == other); }
};

// A texture a node keeps: an image of what the node draws (its layer, or its content) under a
// device transform, over a device area.
struct NodeTexture {
    enum Kind : uint8_t {
        kLayer,          // a plain layer's pixels
        kFilteredLayer,  // an image filter layer's output, under the clip it was rendered for
        kContent,        // a node's content (with its clip), drawn without a layer of its own
    };

    NodeTexture() = default;
    ~NodeTexture();
    NodeTexture(const NodeTexture&) = delete;
    NodeTexture& operator=(const NodeTexture&) = delete;

    Kind kind = kLayer;
    TextureOwner owner;
    std::thread::id thread;
    sk_sp<SkImage> image;
    // The device transform the image was rendered at, and the device area it holds under it;
    // the image's (0, 0) is at origin.
    SkM44 ctm;
    SkIRect bounds = SkIRect::MakeEmpty();
    SkIPoint origin = { 0, 0 };
    // The device area the node draws under ctm (bounds is the part rendered), and whether that is
    // all of it (else the texture is only drawn under ctm itself).
    SkIRect extent = SkIRect::MakeEmpty();
    bool extentComplete = true;
    // Whether it was rendered at a raster transform of its own for an animation (a promotion),
    // rather than at the transform it was last drawn at.
    bool promoted = false;
    // What the image shows: the subtree's generation, the layer paint's (filtered layers), the
    // lighting, and the target's color type and space.
    uint32_t content = 0;
    uint32_t paint = 0;
    uint32_t lighting = 0;
    SkColorType colorType = kUnknown_SkColorType;
    const SkColorSpace* colorSpace = nullptr;
    size_t bytes = 0;
    // When it was last drawn: the frame (this thread's count) and the time.
    uint64_t usedFrame = 0;
    int64_t usedMs = 0;
    // Composites drawn from it.
    int hits = 0;
    // Its index in the registry, while registered.
    size_t slot = SIZE_MAX;
};

// What a node remembers between draws for its texture: how it was drawn last, how many draws in a
// row kept its content (and placement) the same, and how many frames in a row moved it.
struct NodeCache {
    std::unique_ptr<NodeTexture> texture;

    // The last draw: its frame, device transform, subtree and layer paint generations.
    uint64_t drawFrame = 0;
    bool drawn = false;
    SkM44 drawCtm;
    uint32_t drawContent = 0;
    uint32_t drawPaint = 0;
    // Draws in a row whose content was the same, at the same transform or a whole-pixel
    // translation of it; frames in a row whose transform changed while the content didn't.
    int stableDraws = 0;
    int motionFrames = 0;
    // How often a texture went stale before it paid for itself: a node that keeps changing after
    // short pauses has to hold still for longer (exponentially) before it gets one again.
    int churn = 0;

    // The frame's decision, so that the frame's other draws of the node (one per damaged rect,
    // the layer atlas's collect pass) draw it the same way.
    enum Decision : uint8_t { kDirect, kExact, kMapped, kMakeExact, kMakePromoted };
    uint64_t decisionFrame = 0;
    bool decided = false;
    SkM44 decisionCtm;
    Decision decision = kDirect;
    // A whole-pixel offset an exact decision draws the texture at.
    SkIPoint offset = { 0, 0 };

    // The cost of drawing the content, and whether it composites the same through a texture
    // (every operation blends source-over, nothing reads what is under it): for the own content
    // (per ownGeneration) and the subtree (per subtreeGeneration).
    bool ownAnalysed = false;
    uint32_t ownAnalysedGeneration = 0;
    int ownCost = 0;
    bool ownLayerSafe = false;
    bool subtreeAnalysed = false;
    uint32_t subtreeAnalysedGeneration = 0;
    int subtreeCost = 0;
    bool subtreeLayerSafe = false;

    // Whether the last frame composited a promoted texture inexactly, and the property generation
    // then (see RasterCache::beginFrame).
    bool unsettled = false;
    uint32_t inexactGeneration = 0;
};

// Whether restoring a layer drawn with this paint only composites the layer's own pixels (see
// RenderNode.cpp).
bool isBatchableLayerPaint(const SkPaint& paint);

class RasterCache {
public:
    // Switches (read once from the environment).
    static bool retainLayers();       // SKIKO_RENDER_NODE_LAYER_RETAIN=0 turns textures of layers off
    static bool retainFilteredLayers(bool graphite);  // ...=plain leaves image filter layers out; 1: Graphite too
    static bool cacheContent(bool graphite);  // SKIKO_RENDER_NODE_RASTER_CACHE: 0 | ganesh (the default) | 1
    static bool promote();            // SKIKO_RENDER_NODE_PROMOTE=0 turns promotions off
    static bool log();                // SKIKO_RENDER_NODE_LAYER_ATLAS=log
    static size_t budget();
    static int64_t maxAgeMs();

    // The owner of the textures drawn onto canvas: none unless it is a Ganesh or Graphite canvas.
    static TextureOwner ownerOf(SkCanvas* canvas);

    // Renders a width x height texture for owner with draw (onto a cleared canvas of target's
    // kind); null over the budget (after freeing what this thread's earlier frames used), at the
    // frame's limit of new textures, or when the surface can't be made. The texture is
    // registered (counted in the budget) once it has its image.
    static std::unique_ptr<NodeTexture> render(SkCanvas* target, const TextureOwner& owner, int width, int height,
                                               const std::function<void(SkCanvas*)>& draw);
    // Whether this frame may still make a new texture.
    static bool mayRenderThisFrame();
    // Notes that texture was drawn now.
    static void used(NodeTexture* texture);

    // The frame count of this thread (see beginFrame), and whether a host counts frames for it.
    static uint64_t frame();
    static bool hostFrames();
    // A node drawn outside any other node's drawing: the start of a frame when no host counts them.
    static void onTopLevelDraw();

    // Set while a texture is being rendered: the nodes it draws draw directly, so that their own
    // bookkeeping only sees their real draws.
    static bool rendering();
    struct RenderingScope {
        RenderingScope();
        ~RenderingScope();
    };

    // Draws on this thread skip the textures and their bookkeeping (a host's reference frame).
    static bool bypassed();
    static void setBypass(bool bypass);

    // The host side (see above). beginFrame: a frame starts on this thread; the nodes whose
    // animation ended since they last composited a promoted texture inexactly are marked changed,
    // and the textures this thread made that went unused for too long are freed.
    static void beginFrame();
    // Whether the frame since beginFrame composited a promoted texture inexactly.
    static bool settleRequested();
    // A node composited its promoted texture inexactly in this frame.
    static void drewInexact(RenderNode* node);

    // Frees the textures this thread made that weren't drawn for msNotUsed (all of them for 0);
    // for owner only when given.
    static void purge(int64_t msNotUsed, const void* owner = nullptr);
    // The bytes held by every texture (all threads), and by those of owner.
    static size_t bytes(const void* owner = nullptr);
    static int count();

    // Graphite: recorder is going away; the textures made with it are released (here, or by the
    // threads that made them), and it is no longer an owner.
    static void onGraphiteRecorderClosing(const void* recorder);
    // Ganesh: context was abandoned or is being freed: its textures go too.
    static void onGaneshContextReleasing(const void* context);

private:
    friend struct NodeTexture;
    static void unregister(NodeTexture* texture);
};

} // namespace node
} // namespace skiko
