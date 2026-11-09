#pragma once
#include <mutex>
#include <vector>
#include <SkDrawable.h>
#include <SkPictureRecorder.h>
#include <SkRect.h>
#include <SkRefCnt.h>

namespace skiko {
namespace node {

class RenderNode;

// Render twins: copies of render nodes that another thread draws, as Android's RenderThread draws
// its own copy of the UI thread's display lists and properties (HWUI's staging and render
// properties, brought over in its sync stage).
//
// A frame is recorded on the nodes' own thread between beginRecording and finishRecording, the
// render nodes it draws recorded by reference (a recording, not a picture of them). sync then
// replays it into a recording that draws each node's twin instead, bringing the twins up to date on
// the way: a node's properties, its recorded content (where it draws other nodes, recorded anew
// with their twins; otherwise the same immutable recording, shared), the nodes it draws. With the
// snapshot cache on, changes relay upwards (see RenderNode::relaySubtreeChange), so only the nodes
// that changed since the last sync, and the nodes drawing them, are visited. The twins are render
// nodes of their own (a context of their own, the original context's lighting), so where they are
// drawn the layer atlas, retained layers and live snapshots apply as they do to any node.
//
// Threads: sync reads the nodes, so the nodes' thread must not change them meanwhile; it changes the
// twins, so a frame it returned must not be drawn meanwhile. A frame sync returned stays valid until
// the next sync; draw it on one thread at a time. A node going away hands its twin back (any
// thread); releasePending releases what was handed back, on the thread that draws the twins, as a
// twin may keep GPU textures made there.
class RenderNodeTwins : public SkRefCnt {
public:
    RenderNodeTwins();
    ~RenderNodeTwins() override;

    // The nodes' thread: starts a frame of width x height, recorded through the returned canvas.
    SkCanvas* beginRecording(float width, float height);
    // The nodes' thread: the frame as recorded, drawing the nodes themselves.
    sk_sp<SkDrawable> finishRecording();

    // The frame drawing the twins of the nodes [frame] draws (see above), and the twins up to date.
    sk_sp<SkDrawable> sync(SkDrawable* frame);

    // The drawing thread: releases the twins handed back since the last call.
    void releasePending();

    // A twin (or anything else a twin drew) whose node went away, released by releasePending, or
    // at once after close.
    void handBack(sk_sp<SkRefCnt> object);

    // Releases what is pending (call on the drawing thread); twins handed back later go at once.
    void close();

    // What the last sync did, for logs: the nodes visited, and of them the ones recorded anew.
    int visitedNodes() const { return this->visited; }
    int recordedNodes() const { return this->recorded; }

private:
    friend class RenderNode;

    SkPictureRecorder recorder;
    SkRect bounds = SkRect::MakeEmpty();
    std::mutex mutex;
    std::vector<sk_sp<SkRefCnt>> pending;
    bool closed = false;
    int visited = 0;
    int recorded = 0;
    // This sync's number, unique across all twins (see RenderNode::relaySubtreeChange).
    uint64_t syncSerial = 0;
};

} // namespace node
} // namespace skiko
