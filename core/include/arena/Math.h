#pragma once
// Small 2D math kit for the simulation. The core library does not use raylib's
// types so it can be tested and run headless.
#include <cmath>
#include <cstdint>

namespace arena {

inline constexpr float kPi = 3.14159265358979323846f;

struct Vec2 {
    float x = 0.0f;
    float y = 0.0f;

    constexpr Vec2() = default;
    constexpr Vec2(float x_, float y_) : x(x_), y(y_) {}

    constexpr Vec2 operator+(Vec2 o) const { return {x + o.x, y + o.y}; }
    constexpr Vec2 operator-(Vec2 o) const { return {x - o.x, y - o.y}; }
    constexpr Vec2 operator*(float s) const { return {x * s, y * s}; }
    constexpr Vec2 operator/(float s) const { return {x / s, y / s}; }
    constexpr Vec2 operator-() const { return {-x, -y}; }
    constexpr Vec2& operator+=(Vec2 o) { x += o.x; y += o.y; return *this; }
    constexpr Vec2& operator-=(Vec2 o) { x -= o.x; y -= o.y; return *this; }
    constexpr Vec2& operator*=(float s) { x *= s; y *= s; return *this; }
    constexpr bool operator==(const Vec2&) const = default;
};

constexpr Vec2 operator*(float s, Vec2 v) { return v * s; }

constexpr float Dot(Vec2 a, Vec2 b) { return a.x * b.x + a.y * b.y; }
constexpr float Cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }
constexpr float LengthSq(Vec2 v) { return Dot(v, v); }
inline float Length(Vec2 v) { return std::sqrt(LengthSq(v)); }
constexpr float DistanceSq(Vec2 a, Vec2 b) { return LengthSq(a - b); }
inline float Distance(Vec2 a, Vec2 b) { return Length(a - b); }
constexpr Vec2 Perp(Vec2 v) { return {-v.y, v.x}; }
constexpr Vec2 Lerp(Vec2 a, Vec2 b, float t) { return a + (b - a) * t; }
constexpr float Lerp(float a, float b, float t) { return a + (b - a) * t; }
constexpr float Clamp(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

// Returns the zero vector instead of NaN for a zero-length input.
inline Vec2 Normalize(Vec2 v) {
    const float len = Length(v);
    return len > 1e-6f ? v / len : Vec2{};
}

inline Vec2 ClampLength(Vec2 v, float maxLen) {
    const float lenSq = LengthSq(v);
    if (lenSq <= maxLen * maxLen) return v;
    return v * (maxLen / std::sqrt(lenSq));
}

inline Vec2 FromAngle(float radians) { return {std::cos(radians), std::sin(radians)}; }
inline float Angle(Vec2 v) { return std::atan2(v.y, v.x); }

// Moves `current` toward `target` by at most `maxDelta` (length of the change).
inline Vec2 MoveToward(Vec2 current, Vec2 target, float maxDelta) {
    const Vec2 delta = target - current;
    const float distSq = LengthSq(delta);
    if (distSq <= maxDelta * maxDelta) return target;
    return current + delta * (maxDelta / std::sqrt(distSq));
}

// True if `point` is inside the circular sector at `origin` facing `dir`
// (unit vector) with the given radius and half-angle. `pad` grows the radius,
// which is how a target's own radius is taken into account.
inline bool InSector(Vec2 origin, Vec2 dir, float radius, float halfAngle, Vec2 point, float pad) {
    const Vec2 to = point - origin;
    const float distSq = LengthSq(to);
    const float r = radius + pad;
    if (distSq > r * r) return false;
    if (distSq < pad * pad) return true;  // overlapping the origin: always a hit
    const float cosAngle = Dot(to, dir) / std::sqrt(distSq);
    return cosAngle >= std::cos(halfAngle);
}

// SplitMix64: tiny, fast and fully deterministic across platforms, so the
// same seed and inputs always replay the same fight.
struct Rng {
    std::uint64_t state = 0x9E3779B97F4A7C15ull;

    constexpr explicit Rng(std::uint64_t seed = 1) : state(seed) {}

    constexpr std::uint64_t NextU64() {
        std::uint64_t z = (state += 0x9E3779B97F4A7C15ull);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        return z ^ (z >> 31);
    }
    // [0, 1), 24 bits of mantissa
    constexpr float Next01() { return static_cast<float>(NextU64() >> 40) * (1.0f / 16777216.0f); }
    constexpr float Range(float lo, float hi) { return lo + (hi - lo) * Next01(); }
    constexpr int RangeInt(int lo, int hiInclusive) {
        const auto span = static_cast<std::uint64_t>(hiInclusive - lo + 1);
        return lo + static_cast<int>(NextU64() % span);
    }
};

}  // namespace arena
