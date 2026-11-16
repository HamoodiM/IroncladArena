#pragma once
// Everything that draws. Reads the World, never changes it. Positions are
// interpolated between the last two simulation ticks with `alpha`, so motion
// is smooth at any refresh rate.
#include <array>
#include <cstdint>

#include <raylib.h>

#include "arena/World.h"

namespace game {

struct FrameStats {
    std::array<float, 180> frameMs{};
    int head = 0;
    int ticksThisFrame = 0;
    std::uint64_t allocsThisFrame = 0;
    std::uint64_t allocsMaxDuringWave = 0;  // reset at each wave start
    void Push(float ms) {
        frameMs[static_cast<std::size_t>(head)] = ms;
        head = (head + 1) % static_cast<int>(frameMs.size());
    }
};

class Renderer {
public:
    void Reset();
    void OnEvents(const arena::World& world);  // after each Step
    void Update(float frameSeconds, const arena::World& world, float alpha);

    Camera2D WorldCamera(int screenW, int screenH, bool withShake) const;
    void DrawWorld(const arena::World& world, float alpha, bool debug) const;
    void DrawHud(const arena::World& world, int screenW, int screenH, bool feelOn, int best) const;
    void DrawDebugPanel(const arena::World& world, const FrameStats& stats, int screenW, int screenH) const;

    bool shakeEnabled = true;

private:
    struct Fx {
        enum class Kind : std::uint8_t { Ring, Slam, Flash } kind = Kind::Ring;
        Vector2 pos{};
        float age = 0.0f;
        float life = 0.3f;
        float radius = 30.0f;
        Color color{};
    };
    struct Ghost {
        Vector2 pos{};
        float age = 99.0f;
    };

    void AddFx(const Fx& fx);
    void AddTrauma(float amount);

    std::array<Fx, 96> m_fx{};
    int m_fxCount = 0;
    std::array<Ghost, 12> m_ghosts{};
    int m_ghostHead = 0;
    float m_ghostTimer = 0.0f;
    float m_trauma = 0.0f;
    Vector2 m_shake{};
    float m_time = 0.0f;
    arena::Vec2 m_camTarget{800.0f, 480.0f};
    bool m_camSnapped = false;
    std::uint32_t m_seed = 777;

    char m_banner[32] = {};
    float m_bannerAge = 99.0f;
    float m_hurtFlash = 0.0f;
};

}  // namespace game
