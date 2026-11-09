#pragma once
#include <cstdint>
#include <vector>
#include <SkRefCnt.h>

namespace skiko {
namespace node {

class RenderNode;

// The properties a RenderNodeAnimationClock animates: the ones that change how a node's recorded content is drawn
// (graphicsLayer's), never the content, so an animated frame replays the recordings it has.
enum class AnimatedProperty : int32_t {
    Alpha = 0,
    TranslationX,
    TranslationY,
    ScaleX,
    ScaleY,
    RotationX,
    RotationY,
    RotationZ,
};
constexpr int kAnimatedPropertyCount = 8;

// How an animated alpha is drawn, as Compose's CompositingStrategy: Auto draws the content through a layer while the
// alpha is below 1 (a graphicsLayer's alpha), Offscreen always through a layer, ModulateAlpha never (each of the
// content's paints is multiplied). The clock owns the node's layer paint under Auto and Offscreen.
enum class AlphaCompositing : int32_t {
    Auto = 0,
    Offscreen,
    ModulateAlpha,
};

// Compose's easings a curve can be evaluated with away from Kotlin: LinearEasing, and CubicBezierEasing(a, b, c, d)
// (FastOutSlowInEasing and the other standard ones are cubic). The arithmetic is Compose's, operation for operation
// and in the same precision, so that a value equals the one Compose computes.
class AnimationEasing {
public:
    static AnimationEasing linear();
    static AnimationEasing cubic(float a, float b, float c, float d);

    float transform(float fraction) const;

private:
    bool isCubic = false;
    float a = 0.0f, b = 0.0f, c = 0.0f, d = 0.0f;
    float min = 0.0f, max = 1.0f;
};

struct AnimationKeyframe {
    int32_t timeMillis;
    float value;
    // Applies from this keyframe to the next.
    AnimationEasing easing;
};

// One of Compose's finite animation specs for a Float, evaluated as Compose evaluates it: TweenSpec (FloatTweenSpec),
// SpringSpec (FloatSpringSpec over SpringSimulation, its duration from estimateAnimationDurationMillis), SnapSpec and
// KeyframesSpec (linear interpolation between keyframes, ArcMode.ArcLinear). The functions are those of a
// TargetBasedAnimation made of it: past the duration the value is the target.
class AnimationCurve {
public:
    enum class Kind : int32_t { Tween = 0, Spring = 1, Keyframes = 2, Snap = 3 };

    static AnimationCurve tween(int32_t durationMillis, int32_t delayMillis, const AnimationEasing& easing);
    static AnimationCurve spring(float dampingRatio, float stiffness, float visibilityThreshold);
    static AnimationCurve snap(int32_t delayMillis);
    static AnimationCurve keyframes(int32_t durationMillis, int32_t delayMillis, std::vector<AnimationKeyframe> keyframes);

    // A curve encoded as the Kotlin binding encodes it (RenderNodeAnimationCurve): [kind, ...]. False when malformed.
    static bool decode(const float* data, int size, AnimationCurve* result);

    // TargetBasedAnimation.getValueFromNanos / getVelocityFromNanos / durationNanos of an animation of this curve.
    float value(int64_t playTimeNanos, float initial, float target, float initialVelocity) const;
    float velocity(int64_t playTimeNanos, float initial, float target, float initialVelocity) const;
    int64_t durationNanos(float initial, float target, float initialVelocity) const;

private:
    float specValue(int64_t playTimeNanos, float initial, float target, float initialVelocity) const;
    float specVelocity(int64_t playTimeNanos, float initial, float target, float initialVelocity) const;
    // interpolated: whether the value is interpolated between keyframes (not a keyframe's, the start or the end).
    float keyframesValue(int64_t playTimeNanos, float initial, float target, bool* interpolated) const;

    Kind kind = Kind::Snap;
    int32_t durationMillis = 0;
    int32_t delayMillis = 0;
    AnimationEasing easing;
    float dampingRatio = 1.0f;
    float stiffness = 1500.0f;
    float visibilityThreshold = 0.01f;
    // Keyframes: every timestamp (0 and the duration included, sorted), and the keyframes declared at them.
    std::vector<int32_t> timestamps;
    std::vector<AnimationKeyframe> frames;
};

// Animates render node properties on a frame clock, without the framework that made the nodes: a property's value is
// a curve of the frame time (Compose's, see AnimationCurve), applied to the node before a frame is drawn (tick). The
// node then counts as changed for damage tracking and is drawn from the recordings it has, so an animated frame
// records nothing; while it animates its alpha only, a layer keeps its texture and is composited again.
//
// An animation starts at a given frame time (the frame a framework would have started it on) or at the next tick, from
// a value or where the property is, with a velocity or the one of the animation it replaces (Compose's Animatable:
// interrupted, it continues from the last frame's value, velocity and time). The value at the start is applied at once;
// at the end the target is, and the animation ends. A clock is owned by one thread, like the nodes, and holds the
// nodes it animates while they animate.
class RenderNodeAnimationClock : public SkRefCnt {
public:
    // A start time: at the first tick after the start; continuing from the last frame of the animation replaced.
    static constexpr int64_t kStartOnNextFrame = INT64_MIN;
    static constexpr int64_t kContinue = INT64_MIN + 1;

    RenderNodeAnimationClock();
    ~RenderNodeAnimationClock() override;

    // Starts animating property of node to target, replacing (ending) an animation of it. from and velocity NaN: the
    // property's value and the replaced animation's velocity (0 without one). Returns the animation's id (> 0).
    int64_t start(RenderNode* node, AnimatedProperty property, const AnimationCurve& curve, float from, float target,
                  float velocity, int64_t startTimeNanos, float durationScale, AlphaCompositing compositing);
    // Ends the animation of property where it is.
    void cancel(RenderNode* node, AnimatedProperty property);
    // Ends every animation of node where it is.
    void cancelAll(RenderNode* node);
    // Sets property to value now, ending an animation of it.
    void snap(RenderNode* node, AnimatedProperty property, float value, AlphaCompositing compositing);

    // Applies every animation's value at frameTimeNanos; returns whether a property changed.
    bool tick(int64_t frameTimeNanos);

    bool isActive() const { return this->runningCount > 0; }
    bool isRunning(int64_t id) const;
    // The property's value as last applied, and its animation's velocity at the last frame (0 when still).
    float value(RenderNode* node, AnimatedProperty property) const;
    float velocity(RenderNode* node, AnimatedProperty property) const;
    // The value property's animation has at frameTimeNanos (as last applied when it does not animate).
    float valueAt(RenderNode* node, AnimatedProperty property, int64_t frameTimeNanos) const;
    // How many animations ended (finished, replaced or cancelled) since the clock was made.
    int64_t endedCount() const { return this->ended; }

    // The property as the node has it, and setting it the way the clock does.
    static float propertyValue(RenderNode* node, AnimatedProperty property);
    static void applyProperty(RenderNode* node, AnimatedProperty property, float value, AlphaCompositing compositing);

private:
    struct Animation {
        int64_t id;
        AnimatedProperty property;
        AnimationCurve curve;
        float initial, target, initialVelocity;
        int64_t durationNanos;
        int64_t startTimeNanos;
        float durationScale;
        float lastValue, lastVelocity;
        int64_t lastFrameTimeNanos;
        bool ticked;

        int64_t playTimeNanos(int64_t frameTimeNanos) const;
    };
    struct Entry {
        sk_sp<RenderNode> node;
        AlphaCompositing compositing = AlphaCompositing::Auto;
        std::vector<Animation> animations;
    };

    Entry* find(RenderNode* node) const;
    const Animation* findAnimation(RenderNode* node, AnimatedProperty property) const;
    void endAnimation(Entry* entry, size_t index);
    void updateNodeMask(Entry* entry);
    void sweep();

    std::vector<Entry> entries;
    int64_t nextId = 1;
    int64_t ended = 0;
    int runningCount = 0;
};

} // namespace node
} // namespace skiko
