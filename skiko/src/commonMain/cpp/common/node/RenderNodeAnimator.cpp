// The curves below are Compose's animation specs for a Float (androidx.compose.animation.core 1.13), ported operation for
// operation: the same order, the same float or double precision at each step, the same casts. A value computed here
// equals the one Compose computes for the same play time, so an animation run here draws the frames Compose would
// have. No fused multiply-adds (Kotlin has none) for that.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(_MSC_VER)
#pragma fp_contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <optional>
#include <SkPaint.h>
#include "node/RenderNode.h"
#include "node/RenderNodeAnimator.h"

namespace skiko {
namespace node {

namespace {

constexpr int64_t kMillisToNanos = 1000000LL;
constexpr int64_t kSecondsToMillis = 1000LL;
constexpr double kTau = 3.141592653589793 * 2.0; // kotlin.math.PI * 2.0
constexpr double kEpsilon = 1e-7;
constexpr float kFloatEpsilon = 1.05e-6f;
constexpr float kOneUlpAt1 = 1.1920929e-7f;
constexpr int64_t kMaxLongMillis = INT64_MAX / 1000000LL;

// Kotlin's Float.toLong() and Double.toLong(): truncation, NaN to 0, saturated at the range.
int64_t kotlinToLong(double value) {
    if (std::isnan(value)) {
        return 0;
    }
    if (value >= 9223372036854775807.0) {
        return INT64_MAX;
    }
    if (value <= -9223372036854775808.0) {
        return INT64_MIN;
    }
    return static_cast<int64_t>(value);
}

inline float coerceIn(float value, float min, float max) {
    float v = value < min ? min : value;
    return v > max ? max : v;
}

inline double coerceIn(double value, double min, double max) {
    double v = value < min ? min : value;
    return v > max ? max : v;
}

inline int64_t coerceIn(int64_t value, int64_t min, int64_t max) {
    int64_t v = value < min ? min : value;
    return v > max ? max : v;
}

inline bool closeTo(double a, double b) { return std::fabs(a - b) < kEpsilon; }

inline bool isNotFinite(double value) { return !std::isfinite(value); }

// kotlin.math.max/min on doubles and floats: NaN if either is.
inline double kmax(double a, double b) { return (std::isnan(a) || std::isnan(b)) ? NAN : (a > b ? a : (b > a ? b : (std::signbit(a) ? b : a))); }
inline float kmaxf(float a, float b) { return (std::isnan(a) || std::isnan(b)) ? NAN : (a > b ? a : (b > a ? b : (std::signbit(a) ? b : a))); }
inline float kminf(float a, float b) { return (std::isnan(a) || std::isnan(b)) ? NAN : (a < b ? a : (b < a ? b : (std::signbit(a) ? a : b))); }

// androidx.compose.ui.util.lerp
inline float lerp(float start, float stop, float fraction) {
    return (1 - fraction) * start + fraction * stop;
}

// androidx.compose.ui.util.fastCbrt
float fastCbrt(float x) {
    int32_t bits;
    std::memcpy(&bits, &x, sizeof(bits));
    int64_t v = static_cast<int64_t>(bits) & 0x1ffffffffLL;
    int32_t estimateBits = static_cast<int32_t>(static_cast<uint32_t>(0x2a510554) + static_cast<uint32_t>(static_cast<int32_t>(v / 3)));
    float estimate;
    std::memcpy(&estimate, &estimateBits, sizeof(estimate));
    estimate -= (estimate - x / (estimate * estimate)) * (1.0f / 3.0f);
    estimate -= (estimate - x / (estimate * estimate)) * (1.0f / 3.0f);
    return estimate;
}

// Bezier.kt
float clampValidRootInUnitRange(float r) {
    float s = coerceIn(r, 0.0f, 1.0f);
    return std::fabs(s - r) > kFloatEpsilon ? NAN : s;
}

int writeValidRootInUnitRange(float r, float* roots, int index) {
    float v = clampValidRootInUnitRange(r);
    roots[index] = v;
    return std::isnan(v) ? 0 : 1;
}

float evaluateCubic4(float p0, float p1, float p2, float p3, float t) {
    float a = p3 + 3.0f * (p1 - p2) - p0;
    float b = 3.0f * (p2 - 2.0f * p1 + p0);
    float c = 3.0f * (p1 - p0);
    return ((a * t + b) * t + c) * t + p0;
}

float evaluateCubic2(float p1, float p2, float t) {
    float a = 1.0f / 3.0f + (p1 - p2);
    float b = (p2 - 2.0f * p1);
    float c = p1;
    return 3.0f * ((a * t + b) * t + c) * t;
}

int findQuadraticRoots(float p0, float p1, float p2, float* roots, int index) {
    double a = p0;
    double b = p1;
    double c = p2;
    double d = a - 2.0 * b + c;
    int rootCount = 0;
    if (d != 0.0) {
        double v1 = -std::sqrt(b * b - a * c);
        double v2 = -a + b;
        rootCount += writeValidRootInUnitRange(static_cast<float>(-(v1 + v2) / d), roots, index);
        rootCount += writeValidRootInUnitRange(static_cast<float>((v1 - v2) / d), roots, index + rootCount);
        if (rootCount > 1) {
            float s = roots[index];
            float t = roots[index + 1];
            if (s > t) {
                roots[index] = t;
                roots[index + 1] = s;
            } else if (s == t) {
                rootCount--;
            }
        }
    } else if (b != c) {
        rootCount += writeValidRootInUnitRange(static_cast<float>((2.0 * b - c) / (2.0 * b - 2.0 * c)), roots, index);
    }
    return rootCount;
}

int findLineRoot(float p0, float p1, float* roots, int index) {
    return writeValidRootInUnitRange(-p0 / (p1 - p0), roots, index);
}

void computeCubicVerticalBounds(float p0y, float p1y, float p2y, float p3y, float* minY, float* maxY) {
    float roots[5];
    float d0 = 3.0f * (p1y - p0y);
    float d1 = 3.0f * (p2y - p1y);
    float d2 = 3.0f * (p3y - p2y);
    int count = findQuadraticRoots(d0, d1, d2, roots, 0);
    float dd0 = 2.0f * (d1 - d0);
    float dd1 = 2.0f * (d2 - d1);
    count += findLineRoot(dd0, dd1, roots, count);
    float lo = kminf(p0y, p3y);
    float hi = kmaxf(p0y, p3y);
    for (int i = 0; i < count; i++) {
        float t = roots[i];
        float y = evaluateCubic4(p0y, p1y, p2y, p3y, t);
        lo = kminf(lo, y);
        hi = kmaxf(hi, y);
    }
    *minY = lo;
    *maxY = hi;
}

float findFirstCubicRoot(float p0, float p1, float p2, float p3) {
    double a = 3.0 * (p0 - 2.0 * p1 + p2);
    double b = 3.0 * static_cast<double>(p1 - p0);
    double c = p0;
    double d = static_cast<double>(-p0) + 3.0 * static_cast<double>(p1 - p2) + p3;

    if (closeTo(d, 0.0)) {
        if (closeTo(a, 0.0)) {
            if (closeTo(b, 0.0)) {
                return NAN;
            }
            return clampValidRootInUnitRange(static_cast<float>(-c / b));
        } else {
            double q = std::sqrt(b * b - 4.0 * a * c);
            double a2 = 2.0 * a;
            float root = clampValidRootInUnitRange(static_cast<float>((q - b) / a2));
            if (!std::isnan(root)) return root;
            return clampValidRootInUnitRange(static_cast<float>((-b - q) / a2));
        }
    }

    a /= d;
    b /= d;
    c /= d;

    double o3 = (3.0 * b - a * a) / 9.0;
    double q2 = (2.0 * a * a * a - 9.0 * a * b + 27.0 * c) / 54.0;
    double discriminant = q2 * q2 + o3 * o3 * o3;
    double a3 = a / 3.0;

    if (discriminant < 0.0) {
        double mp33 = -(o3 * o3 * o3);
        double r = std::sqrt(mp33);
        double t = -q2 / r;
        double cosPhi = coerceIn(t, -1.0, 1.0);
        double phi = std::acos(cosPhi);
        float t1 = 2.0f * fastCbrt(static_cast<float>(r));

        float root = clampValidRootInUnitRange(static_cast<float>(t1 * std::cos(phi / 3.0) - a3));
        if (!std::isnan(root)) return root;
        root = clampValidRootInUnitRange(static_cast<float>(t1 * std::cos((phi + kTau) / 3.0) - a3));
        if (!std::isnan(root)) return root;
        return clampValidRootInUnitRange(static_cast<float>(t1 * std::cos((phi + 2.0 * kTau) / 3.0) - a3));
    } else if (discriminant == 0.0) {
        float u1 = -fastCbrt(static_cast<float>(q2));
        float root = clampValidRootInUnitRange(2.0f * u1 - static_cast<float>(a3));
        if (!std::isnan(root)) return root;
        return clampValidRootInUnitRange(-u1 - static_cast<float>(a3));
    }

    double sd = std::sqrt(discriminant);
    float u1 = fastCbrt(static_cast<float>(-q2 + sd));
    float v1 = fastCbrt(static_cast<float>(q2 + sd));
    return clampValidRootInUnitRange(static_cast<float>(static_cast<double>(u1 - v1) - a3));
}

// SpringSimulation.updateValues, for the value or the velocity.
struct Motion {
    float value;
    float velocity;
};

Motion springUpdateValues(float dampingRatio, double naturalFreq, float finalPosition, float lastDisplacement,
                          float lastVelocity, int64_t timeElapsed) {
    float adjustedDisplacement = lastDisplacement - finalPosition;
    double deltaT = static_cast<double>(timeElapsed) / 1000.0;
    double dampingRatioSquared = static_cast<double>(dampingRatio) * static_cast<double>(dampingRatio);
    double r = static_cast<double>(-dampingRatio) * naturalFreq;

    double displacement;
    double currentVelocity;

    if (dampingRatio > 1) {
        double s = naturalFreq * std::sqrt(dampingRatioSquared - 1);
        double gammaPlus = r + s;
        double gammaMinus = r - s;
        double coeffB = (gammaMinus * adjustedDisplacement - lastVelocity) / (gammaMinus - gammaPlus);
        double coeffA = adjustedDisplacement - coeffB;
        displacement = (coeffA * std::exp(gammaMinus * deltaT) + coeffB * std::exp(gammaPlus * deltaT));
        currentVelocity = (coeffA * gammaMinus * std::exp(gammaMinus * deltaT) +
                           coeffB * gammaPlus * std::exp(gammaPlus * deltaT));
    } else if (dampingRatio == 1.0f) {
        double coeffA = adjustedDisplacement;
        double coeffB = lastVelocity + naturalFreq * adjustedDisplacement;
        double nFdT = -naturalFreq * deltaT;
        displacement = (coeffA + coeffB * deltaT) * std::exp(nFdT);
        currentVelocity = (((coeffA + coeffB * deltaT) * std::exp(nFdT) * (-naturalFreq)) + coeffB * std::exp(nFdT));
    } else {
        double dampedFreq = naturalFreq * std::sqrt(1 - dampingRatioSquared);
        double cosCoeff = adjustedDisplacement;
        double sinCoeff = ((1 / dampedFreq) * (((-r * adjustedDisplacement) + lastVelocity)));
        double dFdT = dampedFreq * deltaT;
        displacement = (std::exp(r * deltaT) * ((cosCoeff * std::cos(dFdT) + sinCoeff * std::sin(dFdT))));
        currentVelocity = (displacement * r +
                           (std::exp(r * deltaT) *
                            ((-dampedFreq * cosCoeff * std::sin(dFdT) + dampedFreq * sinCoeff * std::cos(dFdT)))));
    }

    return Motion{
        static_cast<float>(displacement + finalPosition),
        static_cast<float>(currentVelocity),
    };
}

// SpringEstimation.kt
double estimateUnderDamped(double firstRootReal, double firstRootImaginary, double p0, double v0, double delta) {
    double r = firstRootReal;
    double c1 = p0;
    double c2 = (v0 - r * c1) / firstRootImaginary;
    double c = std::sqrt(c1 * c1 + c2 * c2);
    return std::log(delta / c) / r;
}

double estimateCriticallyDamped(double firstRootReal, double p0, double v0, double delta) {
    double r = firstRootReal;
    double c1 = p0;
    double c2 = v0 - r * c1;

    double t1 = std::log(std::fabs(delta / c1)) / r;
    double t2;
    {
        double guess = std::log(std::fabs(delta / c2));
        double t = guess;
        for (int i = 0; i <= 5; i++) {
            t = (guess - std::log(std::fabs(t / r)));
        }
        t2 = t / r;
    }
    double tCurr;
    if (isNotFinite(t1)) {
        tCurr = t2;
    } else if (isNotFinite(t2)) {
        tCurr = t1;
    } else {
        tCurr = kmax(t1, t2);
    }

    double tInflection = -(r * c1 + c2) / (r * c2);
    double xInflection = c1 * std::exp(r * tInflection) + c2 * tInflection * std::exp(r * tInflection);

    double signedDelta;
    if (std::isnan(tInflection) || tInflection <= 0.0) {
        signedDelta = -delta;
    } else if (tInflection > 0.0 && -xInflection < delta) {
        if (c2 < 0 && c1 > 0) {
            tCurr = 0.0;
        }
        signedDelta = -delta;
    } else {
        tCurr = -(2.0 / r) - (c1 / c2);
        signedDelta = delta;
    }

    double tDelta = std::numeric_limits<double>::max();
    int iterations = 0;
    while (tDelta > 0.001 && iterations < 100) {
        iterations++;
        double tLast = tCurr;
        double fn = (c1 + c2 * tCurr) * std::exp(r * tCurr) + signedDelta;
        double fnPrime = (c2 * (r * tCurr + 1) + c1 * r) * std::exp(r * tCurr);
        tCurr = tCurr - fn / fnPrime;
        tDelta = std::fabs(tLast - tCurr);
    }
    return tCurr;
}

double estimateOverDamped(double firstRootReal, double secondRootReal, double p0, double v0, double delta) {
    double r1 = firstRootReal;
    double r2 = secondRootReal;
    double c2 = (r1 * p0 - v0) / (r1 - r2);
    double c1 = p0 - c2;

    double t1 = std::log(std::fabs(delta / c1)) / r1;
    double t2 = std::log(std::fabs(delta / c2)) / r2;

    double tCurr;
    if (isNotFinite(t1)) {
        tCurr = t2;
    } else if (isNotFinite(t2)) {
        tCurr = t1;
    } else {
        tCurr = kmax(t1, t2);
    }

    double tInflection = std::log((c1 * r1) / (-c2 * r2)) / (r2 - r1);
    auto xInflection = [&]() { return c1 * std::exp(r1 * tInflection) + c2 * std::exp(r2 * tInflection); };

    double signedDelta;
    if (std::isnan(tInflection) || tInflection <= 0.0) {
        signedDelta = -delta;
    } else if (tInflection > 0.0 && -xInflection() < delta) {
        if (c2 > 0.0 && c1 < 0.0) {
            tCurr = 0.0;
        }
        signedDelta = -delta;
    } else {
        tCurr = std::log(-(c2 * r2 * r2) / (c1 * r1 * r1)) / (r1 - r2);
        signedDelta = delta;
    }

    if (std::fabs(c1 * r1 * std::exp(r1 * tCurr) + c2 * r2 * std::exp(r2 * tCurr)) < 0.0001) {
        return tCurr;
    }
    double tDelta = std::numeric_limits<double>::max();
    int iterations = 0;
    while (tDelta > 0.001 && iterations < 100) {
        iterations++;
        double tLast = tCurr;
        double fn = c1 * std::exp(r1 * tCurr) + c2 * std::exp(r2 * tCurr) + signedDelta;
        double fnPrime = c1 * r1 * std::exp(r1 * tCurr) + c2 * r2 * std::exp(r2 * tCurr);
        tCurr = tCurr - fn / fnPrime;
        tDelta = std::fabs(tLast - tCurr);
    }
    return tCurr;
}

int64_t estimateDurationInternal(double firstRootReal, double firstRootImaginary, double secondRootReal,
                                 double dampingRatio, double initialVelocity, double initialPosition, double delta) {
    if (initialPosition == 0.0 && initialVelocity == 0.0) {
        return 0;
    }
    double v0 = initialPosition < 0 ? -initialVelocity : initialVelocity;
    double p0 = std::fabs(initialPosition);
    double t;
    if (dampingRatio > 1.0) {
        t = estimateOverDamped(firstRootReal, secondRootReal, p0, v0, delta);
    } else if (dampingRatio < 1.0) {
        t = estimateUnderDamped(firstRootReal, firstRootImaginary, p0, v0, delta);
    } else {
        t = estimateCriticallyDamped(firstRootReal, p0, v0, delta);
    }
    return kotlinToLong(t * 1000.0);
}

int64_t estimateAnimationDurationMillis(float stiffnessF, float dampingRatioF, float initialVelocityF,
                                        float initialDisplacementF, float deltaF) {
    if (dampingRatioF == 0.0f) {
        return kMaxLongMillis;
    }
    double stiffness = stiffnessF;
    double dampingRatio = dampingRatioF;
    double initialVelocity = initialVelocityF;
    double initialDisplacement = initialDisplacementF;
    double delta = deltaF;

    double dampingCoefficient = 2.0 * dampingRatio * std::sqrt(stiffness);
    double partialRoot = dampingCoefficient * dampingCoefficient - 4.0 * stiffness;
    double partialRootReal = partialRoot < 0.0 ? 0.0 : std::sqrt(partialRoot);
    double partialRootImaginary = partialRoot < 0.0 ? std::sqrt(std::fabs(partialRoot)) : 0.0;
    double firstRootReal = (-dampingCoefficient + partialRootReal) * 0.5;
    double firstRootImaginary = partialRootImaginary * 0.5;
    double secondRootReal = (-dampingCoefficient - partialRootReal) * 0.5;
    return estimateDurationInternal(firstRootReal, firstRootImaginary, secondRootReal, dampingRatio,
                                    initialVelocity, initialDisplacement, delta);
}

// IntList.binarySearch
int binarySearch(const std::vector<int32_t>& list, int32_t element) {
    int low = 0;
    int high = static_cast<int>(list.size()) - 1;
    while (low <= high) {
        int mid = static_cast<int>(static_cast<unsigned>(low + high) >> 1);
        int32_t midVal = list[mid];
        if (midVal < element) {
            low = mid + 1;
        } else if (midVal > element) {
            high = mid - 1;
        } else {
            return mid;
        }
    }
    return -(low + 1);
}

} // namespace

// ---------------------------------------------------------------------------------------------
// AnimationEasing: LinearEasing, CubicBezierEasing.
// ---------------------------------------------------------------------------------------------

AnimationEasing AnimationEasing::linear() {
    return AnimationEasing();
}

AnimationEasing AnimationEasing::cubic(float a, float b, float c, float d) {
    AnimationEasing easing;
    easing.isCubic = true;
    easing.a = a;
    easing.b = b;
    easing.c = c;
    easing.d = d;
    computeCubicVerticalBounds(0.0f, b, d, 1.0f, &easing.min, &easing.max);
    return easing;
}

float AnimationEasing::transform(float fraction) const {
    if (!this->isCubic) {
        return fraction;
    }
    if (fraction > 0.0f && fraction < 1.0f) {
        float f = kmaxf(fraction, kOneUlpAt1);
        float t = findFirstCubicRoot(0.0f - f, this->a - f, this->c - f, 1.0f - f);
        if (std::isnan(t)) {
            // Compose throws: the curve has no solution here.
            return NAN;
        }
        return coerceIn(evaluateCubic2(this->b, this->d, t), this->min, this->max);
    }
    return fraction;
}

// ---------------------------------------------------------------------------------------------
// AnimationCurve.
// ---------------------------------------------------------------------------------------------

AnimationCurve AnimationCurve::tween(int32_t durationMillis, int32_t delayMillis, const AnimationEasing& easing) {
    AnimationCurve curve;
    curve.kind = Kind::Tween;
    curve.durationMillis = durationMillis;
    curve.delayMillis = delayMillis;
    curve.easing = easing;
    return curve;
}

AnimationCurve AnimationCurve::spring(float dampingRatio, float stiffness, float visibilityThreshold) {
    AnimationCurve curve;
    curve.kind = Kind::Spring;
    curve.dampingRatio = dampingRatio;
    curve.stiffness = stiffness;
    curve.visibilityThreshold = visibilityThreshold;
    return curve;
}

AnimationCurve AnimationCurve::snap(int32_t delayMillis) {
    AnimationCurve curve;
    curve.kind = Kind::Snap;
    curve.delayMillis = delayMillis;
    return curve;
}

AnimationCurve AnimationCurve::keyframes(int32_t durationMillis, int32_t delayMillis, std::vector<AnimationKeyframe> keyframes) {
    AnimationCurve curve;
    curve.kind = Kind::Keyframes;
    curve.durationMillis = durationMillis;
    curve.delayMillis = delayMillis;
    // KeyframesSpec.vectorize: one keyframe a time (a later one replaces), and the timestamps with 0 and the duration.
    std::vector<AnimationKeyframe> frames;
    for (const AnimationKeyframe& keyframe : keyframes) {
        auto same = std::find_if(frames.begin(), frames.end(),
                                 [&](const AnimationKeyframe& f) { return f.timeMillis == keyframe.timeMillis; });
        if (same != frames.end()) {
            *same = keyframe;
        } else {
            frames.push_back(keyframe);
        }
    }
    std::vector<int32_t> timestamps;
    bool hasStart = false, hasEnd = false;
    for (const AnimationKeyframe& frame : frames) {
        timestamps.push_back(frame.timeMillis);
        hasStart |= frame.timeMillis == 0;
        hasEnd |= frame.timeMillis == durationMillis;
    }
    if (!hasStart) timestamps.push_back(0);
    if (!hasEnd) timestamps.push_back(durationMillis);
    std::sort(timestamps.begin(), timestamps.end());
    curve.timestamps = std::move(timestamps);
    curve.frames = std::move(frames);
    return curve;
}

static bool decodeEasing(const float* data, int size, int at, AnimationEasing* easing) {
    if (at + 5 > size) {
        return false;
    }
    int kind = static_cast<int>(data[at]);
    if (kind == 0) {
        *easing = AnimationEasing::linear();
        return true;
    }
    if (kind == 1) {
        *easing = AnimationEasing::cubic(data[at + 1], data[at + 2], data[at + 3], data[at + 4]);
        return true;
    }
    return false;
}

bool AnimationCurve::decode(const float* data, int size, AnimationCurve* result) {
    if (!data || size < 1) {
        return false;
    }
    switch (static_cast<Kind>(static_cast<int>(data[0]))) {
        case Kind::Tween: {
            AnimationEasing easing;
            if (size < 8 || !decodeEasing(data, size, 3, &easing)) return false;
            *result = tween(static_cast<int32_t>(data[1]), static_cast<int32_t>(data[2]), easing);
            return true;
        }
        case Kind::Spring:
            if (size < 4) return false;
            *result = spring(data[1], data[2], data[3]);
            return true;
        case Kind::Snap:
            if (size < 2) return false;
            *result = snap(static_cast<int32_t>(data[1]));
            return true;
        case Kind::Keyframes: {
            if (size < 4) return false;
            int count = static_cast<int>(data[3]);
            if (count < 0 || size < 4 + count * 7) return false;
            std::vector<AnimationKeyframe> keyframes;
            for (int i = 0; i < count; i++) {
                int at = 4 + i * 7;
                AnimationKeyframe keyframe{static_cast<int32_t>(data[at]), data[at + 1], AnimationEasing::linear()};
                if (!decodeEasing(data, size, at + 2, &keyframe.easing)) return false;
                keyframes.push_back(keyframe);
            }
            *result = AnimationCurve::keyframes(static_cast<int32_t>(data[1]), static_cast<int32_t>(data[2]), std::move(keyframes));
            return true;
        }
    }
    return false;
}

// VectorizedKeyframesSpec.getValueFromNanos (no ArcSpline: every keyframe ArcLinear).
float AnimationCurve::keyframesValue(int64_t playTimeNanos, float initial, float target) const {
    int64_t playTimeMillis = playTimeNanos / kMillisToNanos;
    int32_t clampedPlayTime = static_cast<int32_t>(coerceIn(playTimeMillis - this->delayMillis, int64_t(0),
                                                            static_cast<int64_t>(this->durationMillis)));
    for (const AnimationKeyframe& frame : this->frames) {
        if (frame.timeMillis == clampedPlayTime) {
            return frame.value;
        }
    }
    if (clampedPlayTime >= this->durationMillis) {
        return target;
    } else if (clampedPlayTime <= 0) {
        return initial;
    }
    int index = binarySearch(this->timestamps, clampedPlayTime);
    if (index < -1) {
        index = -(index + 2);
    }
    float easedTime;
    int lastIndex = static_cast<int>(this->timestamps.size()) - 1;
    if (index >= lastIndex) {
        easedTime = static_cast<float>(clampedPlayTime) / static_cast<float>(kSecondsToMillis);
    } else {
        int32_t timeMin = this->timestamps[index];
        int32_t timeMax = this->timestamps[index + 1];
        if (clampedPlayTime == timeMin) {
            easedTime = static_cast<float>(timeMin) / static_cast<float>(kSecondsToMillis);
        } else {
            int32_t timeRange = timeMax - timeMin;
            const AnimationEasing* easing = nullptr;
            for (const AnimationKeyframe& frame : this->frames) {
                if (frame.timeMillis == timeMin) easing = &frame.easing;
            }
            float rawFraction = static_cast<float>(clampedPlayTime - timeMin) / static_cast<float>(timeRange);
            easedTime = easing ? easing->transform(rawFraction) : rawFraction;
        }
    }
    float startValue = initial;
    float endValue = target;
    for (const AnimationKeyframe& frame : this->frames) {
        if (frame.timeMillis == this->timestamps[index]) startValue = frame.value;
        if (index + 1 <= lastIndex && frame.timeMillis == this->timestamps[index + 1]) endValue = frame.value;
    }
    return lerp(startValue, endValue, easedTime);
}

float AnimationCurve::specValue(int64_t playTimeNanos, float initial, float target, float initialVelocity) const {
    switch (this->kind) {
        case Kind::Tween: {
            // FloatTweenSpec.getValueFromNanos
            int64_t durationNanos = this->durationMillis * kMillisToNanos;
            int64_t delayNanos = this->delayMillis * kMillisToNanos;
            int64_t clamped = coerceIn(playTimeNanos - delayNanos, int64_t(0), durationNanos);
            float rawFraction = this->durationMillis == 0 ? 1.0f : static_cast<float>(clamped) / static_cast<float>(durationNanos);
            float fraction = this->easing.transform(rawFraction);
            return lerp(initial, target, fraction);
        }
        case Kind::Spring: {
            // FloatSpringSpec.getValueFromNanos
            int64_t playTimeMillis = playTimeNanos / kMillisToNanos;
            double naturalFreq = std::sqrt(static_cast<double>(this->stiffness));
            return springUpdateValues(this->dampingRatio, naturalFreq, target, initial, initialVelocity, playTimeMillis).value;
        }
        case Kind::Snap:
            return playTimeNanos < this->delayMillis * kMillisToNanos ? initial : target;
        case Kind::Keyframes:
            return this->keyframesValue(playTimeNanos, initial, target);
    }
    return target;
}

float AnimationCurve::specVelocity(int64_t playTimeNanos, float initial, float target, float initialVelocity) const {
    switch (this->kind) {
        case Kind::Tween: {
            // FloatTweenSpec.getVelocityFromNanos (which passes the clamped time on, the delay subtracted again)
            int64_t durationNanos = this->durationMillis * kMillisToNanos;
            int64_t delayNanos = this->delayMillis * kMillisToNanos;
            int64_t clamped = coerceIn(playTimeNanos - delayNanos, int64_t(0), durationNanos);
            if (clamped == 0) {
                return initialVelocity;
            }
            float startNum = this->specValue(clamped - kMillisToNanos, initial, target, initialVelocity);
            float endNum = this->specValue(clamped, initial, target, initialVelocity);
            return (endNum - startNum) * 1000.0f;
        }
        case Kind::Spring: {
            int64_t playTimeMillis = playTimeNanos / kMillisToNanos;
            double naturalFreq = std::sqrt(static_cast<double>(this->stiffness));
            return springUpdateValues(this->dampingRatio, naturalFreq, target, initial, initialVelocity, playTimeMillis).velocity;
        }
        case Kind::Snap:
            return initialVelocity;
        case Kind::Keyframes: {
            // VectorizedKeyframesSpec.getVelocityFromNanos without ArcSpline (start minus end, as Compose has it)
            int64_t playTimeMillis = playTimeNanos / kMillisToNanos;
            int64_t clamped = coerceIn(playTimeMillis - this->delayMillis, int64_t(0), static_cast<int64_t>(this->durationMillis));
            if (clamped < 0) {
                return initialVelocity;
            }
            float startNum = this->specValue((clamped - 1) * kMillisToNanos, initial, target, initialVelocity);
            float endNum = this->specValue(clamped * kMillisToNanos, initial, target, initialVelocity);
            return (startNum - endNum) * 1000.0f;
        }
    }
    return 0.0f;
}

int64_t AnimationCurve::durationNanos(float initial, float target, float initialVelocity) const {
    switch (this->kind) {
        case Kind::Tween:
            return this->delayMillis * kMillisToNanos + this->durationMillis * kMillisToNanos;
        case Kind::Keyframes:
            return static_cast<int64_t>(this->delayMillis + this->durationMillis) * kMillisToNanos;
        case Kind::Snap:
            return static_cast<int64_t>(this->delayMillis) * kMillisToNanos;
        case Kind::Spring: {
            // FloatSpringSpec.getDurationNanos: the simulation's stiffness is the square of its sqrt, as a Float.
            double naturalFreq = std::sqrt(static_cast<double>(this->stiffness));
            float stiffness = static_cast<float>(naturalFreq * naturalFreq);
            int64_t millis = estimateAnimationDurationMillis(stiffness, this->dampingRatio,
                                                             initialVelocity / this->visibilityThreshold,
                                                             (initial - target) / this->visibilityThreshold, 1.0f);
            // A Long product, wrapping as Kotlin's does.
            return static_cast<int64_t>(static_cast<uint64_t>(millis) * static_cast<uint64_t>(kMillisToNanos));
        }
    }
    return 0;
}

float AnimationCurve::value(int64_t playTimeNanos, float initial, float target, float initialVelocity) const {
    if (playTimeNanos >= this->durationNanos(initial, target, initialVelocity)) {
        return target;
    }
    return this->specValue(playTimeNanos, initial, target, initialVelocity);
}

float AnimationCurve::velocity(int64_t playTimeNanos, float initial, float target, float initialVelocity) const {
    int64_t duration = this->durationNanos(initial, target, initialVelocity);
    if (playTimeNanos < duration) {
        return this->specVelocity(playTimeNanos, initial, target, initialVelocity);
    }
    // The spec's end velocity: a spring's is 0, the others' the velocity at the duration.
    if (this->kind == Kind::Spring) {
        return 0.0f;
    }
    return this->specVelocity(duration, initial, target, initialVelocity);
}

// ---------------------------------------------------------------------------------------------
// RenderNodeAnimationClock.
// ---------------------------------------------------------------------------------------------

RenderNodeAnimationClock::RenderNodeAnimationClock() = default;

RenderNodeAnimationClock::~RenderNodeAnimationClock() {
    for (Entry& entry : this->entries) {
        entry.node->setAnimatingProperties(0);
    }
}

int64_t RenderNodeAnimationClock::Animation::playTimeNanos(int64_t frameTimeNanos) const {
    if (this->durationScale == 0.0f) {
        return this->durationNanos;
    }
    // SuspendAnimation's doAnimationFrameWithScale: the elapsed nanos (a Long) divided by a Float.
    int64_t elapsed = frameTimeNanos - this->startTimeNanos;
    int64_t playTime = kotlinToLong(static_cast<double>(static_cast<float>(elapsed) / this->durationScale));
    return std::max<int64_t>(playTime, 0);
}

float RenderNodeAnimationClock::propertyValue(RenderNode* node, AnimatedProperty property) {
    switch (property) {
        case AnimatedProperty::Alpha: return node->getAlpha();
        case AnimatedProperty::TranslationX: return node->getTranslationX();
        case AnimatedProperty::TranslationY: return node->getTranslationY();
        case AnimatedProperty::ScaleX: return node->getScaleX();
        case AnimatedProperty::ScaleY: return node->getScaleY();
        case AnimatedProperty::RotationX: return node->getRotationX();
        case AnimatedProperty::RotationY: return node->getRotationY();
        case AnimatedProperty::RotationZ: return node->getRotationZ();
    }
    return 0.0f;
}

void RenderNodeAnimationClock::applyProperty(RenderNode* node, AnimatedProperty property, float value,
                                             AlphaCompositing compositing) {
    switch (property) {
        case AnimatedProperty::Alpha: {
            bool changed = value != node->getAlpha();
            node->setAlpha(value);
            // As Compose's graphicsLayer (SkikoGraphicsLayer.updateLayerProperties): a plain layer paint with the
            // alpha while it needs one, none otherwise.
            bool needsLayer = compositing == AlphaCompositing::Offscreen ||
                              (compositing == AlphaCompositing::Auto && value < 1.0f);
            std::optional<SkPaint>& layerPaint = node->getLayerPaint();
            if (compositing == AlphaCompositing::ModulateAlpha) {
                break;
            }
            if (needsLayer && (changed || !layerPaint || layerPaint->getAlphaf() != value)) {
                SkPaint paint;
                paint.setAlphaf(value);
                node->setLayerPaint(paint);
            } else if (!needsLayer && layerPaint) {
                node->setLayerPaint(std::nullopt);
            }
            break;
        }
        case AnimatedProperty::TranslationX: node->setTranslationX(value); break;
        case AnimatedProperty::TranslationY: node->setTranslationY(value); break;
        case AnimatedProperty::ScaleX: node->setScaleX(value); break;
        case AnimatedProperty::ScaleY: node->setScaleY(value); break;
        case AnimatedProperty::RotationX: node->setRotationX(value); break;
        case AnimatedProperty::RotationY: node->setRotationY(value); break;
        case AnimatedProperty::RotationZ: node->setRotationZ(value); break;
    }
}

RenderNodeAnimationClock::Entry* RenderNodeAnimationClock::find(RenderNode* node) const {
    for (const Entry& entry : this->entries) {
        if (entry.node.get() == node) {
            return const_cast<Entry*>(&entry);
        }
    }
    return nullptr;
}

const RenderNodeAnimationClock::Animation* RenderNodeAnimationClock::findAnimation(RenderNode* node, AnimatedProperty property) const {
    Entry* entry = this->find(node);
    if (!entry) return nullptr;
    for (const Animation& animation : entry->animations) {
        if (animation.property == property) return &animation;
    }
    return nullptr;
}

void RenderNodeAnimationClock::endAnimation(Entry* entry, size_t index) {
    entry->animations.erase(entry->animations.begin() + static_cast<std::ptrdiff_t>(index));
    this->ended++;
    this->runningCount--;
    this->updateNodeMask(entry);
}

void RenderNodeAnimationClock::updateNodeMask(Entry* entry) {
    uint32_t mask = 0;
    for (const Animation& animation : entry->animations) {
        mask |= 1u << static_cast<int>(animation.property);
    }
    entry->node->setAnimatingProperties(mask);
}

void RenderNodeAnimationClock::sweep() {
    this->entries.erase(std::remove_if(this->entries.begin(), this->entries.end(),
                                       [](const Entry& entry) { return entry.animations.empty(); }),
                        this->entries.end());
}

int64_t RenderNodeAnimationClock::start(RenderNode* node, AnimatedProperty property, const AnimationCurve& curve,
                                        float from, float target, float velocity, int64_t startTimeNanos,
                                        float durationScale, AlphaCompositing compositing) {
    Entry* entry = this->find(node);
    if (!entry) {
        this->entries.push_back(Entry{sk_ref_sp(node), compositing, {}});
        entry = &this->entries.back();
    }
    entry->compositing = compositing;

    float initial = std::isnan(from) ? propertyValue(node, property) : from;
    float initialVelocity = std::isnan(velocity) ? 0.0f : velocity;
    int64_t startTime = startTimeNanos == kContinue ? kStartOnNextFrame : startTimeNanos;
    for (size_t i = 0; i < entry->animations.size(); i++) {
        Animation& replaced = entry->animations[i];
        if (replaced.property != property) continue;
        // Animatable.animateTo while running: from the last frame's value and velocity, its time the last frame's.
        if (std::isnan(from)) initial = replaced.lastValue;
        if (std::isnan(velocity)) initialVelocity = replaced.lastVelocity;
        if (startTimeNanos == kContinue && replaced.ticked) startTime = replaced.lastFrameTimeNanos;
        this->endAnimation(entry, i);
        break;
    }

    Animation animation{};
    animation.id = this->nextId++;
    animation.property = property;
    animation.curve = curve;
    animation.initial = initial;
    animation.target = target;
    animation.initialVelocity = initialVelocity;
    animation.durationNanos = curve.durationNanos(initial, target, initialVelocity);
    animation.startTimeNanos = startTime;
    animation.durationScale = durationScale;
    // The first frame, as Compose's animate() has it: the value at play time 0.
    animation.lastValue = curve.value(0, initial, target, initialVelocity);
    if (std::isnan(animation.lastValue)) animation.lastValue = target;
    animation.lastVelocity = initialVelocity;
    animation.lastFrameTimeNanos = startTime;
    animation.ticked = startTime != kStartOnNextFrame;
    applyProperty(node, property, animation.lastValue, compositing);
    entry->animations.push_back(animation);
    this->runningCount++;
    this->updateNodeMask(entry);
    return animation.id;
}

void RenderNodeAnimationClock::cancel(RenderNode* node, AnimatedProperty property) {
    Entry* entry = this->find(node);
    if (!entry) return;
    for (size_t i = 0; i < entry->animations.size(); i++) {
        if (entry->animations[i].property == property) {
            this->endAnimation(entry, i);
            break;
        }
    }
    this->sweep();
}

void RenderNodeAnimationClock::cancelAll(RenderNode* node) {
    Entry* entry = this->find(node);
    if (!entry) return;
    while (!entry->animations.empty()) {
        this->endAnimation(entry, entry->animations.size() - 1);
    }
    this->sweep();
}

void RenderNodeAnimationClock::snap(RenderNode* node, AnimatedProperty property, float value, AlphaCompositing compositing) {
    this->cancel(node, property);
    applyProperty(node, property, value, compositing);
}

bool RenderNodeAnimationClock::tick(int64_t frameTimeNanos) {
    bool changed = false;
    for (Entry& entry : this->entries) {
        RenderNode* node = entry.node.get();
        for (size_t i = 0; i < entry.animations.size();) {
            Animation& animation = entry.animations[i];
            if (animation.startTimeNanos == kStartOnNextFrame) {
                animation.startTimeNanos = frameTimeNanos;
            }
            int64_t playTime = animation.playTimeNanos(frameTimeNanos);
            bool finished = playTime >= animation.durationNanos;
            float value = finished ? animation.target
                                   : animation.curve.value(playTime, animation.initial, animation.target, animation.initialVelocity);
            if (std::isnan(value)) {
                value = animation.target;
                finished = true;
            }
            float before = propertyValue(node, animation.property);
            applyProperty(node, animation.property, value, entry.compositing);
            changed |= before != value;
            animation.lastValue = value;
            animation.lastVelocity = finished ? 0.0f
                : animation.curve.velocity(playTime, animation.initial, animation.target, animation.initialVelocity);
            animation.lastFrameTimeNanos = frameTimeNanos;
            animation.ticked = true;
            if (finished) {
                this->endAnimation(&entry, i);
            } else {
                i++;
            }
        }
    }
    this->sweep();
    return changed;
}

bool RenderNodeAnimationClock::isRunning(int64_t id) const {
    for (const Entry& entry : this->entries) {
        for (const Animation& animation : entry.animations) {
            if (animation.id == id) return true;
        }
    }
    return false;
}

float RenderNodeAnimationClock::value(RenderNode* node, AnimatedProperty property) const {
    const Animation* animation = this->findAnimation(node, property);
    return animation ? animation->lastValue : propertyValue(node, property);
}

float RenderNodeAnimationClock::velocity(RenderNode* node, AnimatedProperty property) const {
    const Animation* animation = this->findAnimation(node, property);
    return animation ? animation->lastVelocity : 0.0f;
}

float RenderNodeAnimationClock::valueAt(RenderNode* node, AnimatedProperty property, int64_t frameTimeNanos) const {
    const Animation* animation = this->findAnimation(node, property);
    if (!animation) return propertyValue(node, property);
    if (animation->startTimeNanos == kStartOnNextFrame) return animation->lastValue;
    int64_t playTime = animation->playTimeNanos(frameTimeNanos);
    return animation->curve.value(playTime, animation->initial, animation->target, animation->initialVelocity);
}

} // namespace node
} // namespace skiko
