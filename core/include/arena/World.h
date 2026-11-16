#pragma once
// The whole simulation. Step() advances exactly one fixed tick from one
// PlayerInput, so the game is deterministic: the same seed and the same inputs
// produce the same fight, bit for bit (see tests/test_determinism.cpp).
//
// Memory: every entity lives in a fixed-capacity Pool and the grid and queues
// are sized in the constructor, so a running wave makes no heap allocations.
// The game's debug overlay counts operator new calls per frame to prove it.
#include <array>
#include <cstdint>
#include <vector>

#include "arena/EnemyBrain.h"
#include "arena/GameData.h"
#include "arena/Math.h"
#include "arena/PlayerMotor.h"
#include "arena/Pool.h"
#include "arena/SpatialGrid.h"

namespace arena {

struct Projectile {
    Vec2 pos, prevPos, vel;
    float radius = 5.0f;
    float damage = 1.0f;
    float knockback = 0.0f;
    float life = 2.0f;
    bool fromPlayer = true;
};

struct Particle {
    Vec2 pos, prevPos, vel;
    float life = 0.0f;
    float maxLife = 1.0f;
    float size = 3.0f;
    float drag = 4.0f;  // fraction of speed lost per second
    std::uint32_t color = 0xFFFFFFFFu;
};

struct Pillar {
    Vec2 pos;
    float radius;
};

enum class EventType : std::uint8_t {
    WaveStarted,
    WaveCleared,
    EnemySpawned,
    EnemyWindup,
    EnemyAttack,
    EnemyHit,
    EnemyStaggered,
    EnemyKilled,
    PlayerSwing,
    PlayerShot,
    PlayerDash,
    PlayerHurt,
    PlayerDied,
    Slam,
    ProjectileHitWall,
};

// What happened this tick, for sound, screen shake and UI. The simulation
// never reads these back.
struct GameEvent {
    EventType type;
    Vec2 pos;
    Vec2 dir;
    float value = 0.0f;  // damage, combo step, wave number...
    int enemyDef = -1;
};

enum class WavePhase : std::uint8_t { Intermission, Spawning, Fighting };

class World {
public:
    static constexpr float kStep = 1.0f / 60.0f;
    static constexpr std::size_t kMaxEnemies = 160;
    static constexpr std::size_t kMaxProjectiles = 384;
    static constexpr std::size_t kMaxParticles = 1536;
    static constexpr std::size_t kMaxEvents = 512;
    static constexpr std::size_t kMaxSpawnQueue = 1024;
    static constexpr Vec2 kArenaMin{0.0f, 0.0f};
    static constexpr Vec2 kArenaMax{1600.0f, 960.0f};

    World(const GameData& data, std::uint64_t seed);
    void Reset(std::uint64_t seed);

    void Step(const PlayerInput& input);

    const GameData& Data() const { return m_data; }
    const PlayerState& Player() const { return m_player; }
    const Pool<Enemy, kMaxEnemies>& Enemies() const { return m_enemies; }
    const Pool<Projectile, kMaxProjectiles>& Projectiles() const { return m_projectiles; }
    const Pool<Particle, kMaxParticles>& Particles() const { return m_particles; }
    const std::array<Pillar, 4>& Pillars() const { return m_pillars; }
    const SpatialGrid& Grid() const { return m_grid; }
    const std::vector<GameEvent>& Events() const { return m_events; }

    int Wave() const { return m_wave; }  // 1-based once the first wave starts
    WavePhase Phase() const { return m_phase; }
    float PhaseTimer() const { return m_phaseTimer; }
    int EnemiesRemaining() const;  // alive + still queued
    int Score() const { return m_score; }
    int Kills() const { return m_kills; }
    std::uint64_t Tick() const { return m_tick; }
    float Hitstop() const { return m_hitstop; }
    bool GameOver() const { return !m_player.alive; }
    std::uint32_t DroppedEvents() const { return m_droppedEvents; }
    float MaxEnemyRadius() const { return m_maxEnemyRadius; }

    // FNV-1a over everything that affects future ticks.
    std::uint64_t StateHash() const;

    FeelSettings feel;

    // Test hooks: place an enemy directly (no spawn queue, already active).
    Handle DebugSpawnEnemy(int def, Vec2 pos, EnemyState state = EnemyState::Chase);
    PlayerState& DebugPlayer() { return m_player; }
    Enemy* DebugEnemy(Handle h) { return m_enemies.Get(h); }
    void DebugSetWavesEnabled(bool on) { m_wavesEnabled = on; }
    void DebugFillParticles();  // fills the pool with long-lived particles

private:
    void Emit(EventType type, Vec2 pos, Vec2 dir = {}, float value = 0.0f, int enemyDef = -1);
    void BuildEnemyGrid();
    void UpdateWaves(float dt);
    void StartWave();
    bool TrySpawnFromQueue();
    void ApplyPlayerActions(const PlayerActions& actions);
    void UpdateEnemies(float dt);
    void ResolveEnemyOverlaps();
    void UpdateProjectiles(float dt);
    void UpdateParticles(float dt);
    void DamageEnemy(std::uint32_t index, float damage, Vec2 dir, float knockback);
    bool DamagePlayer(float damage, Vec2 dir, float knockback);  // false if invulnerable or dead
    void ResolveStatic(Vec2& pos, Vec2& vel, float radius) const;
    bool HitsStatic(Vec2 pos, float radius) const;
    void Burst(Vec2 pos, int count, std::uint32_t color, float speedMin, float speedMax, float life, float size,
               Vec2 dir = {}, float spread = kPi);
    void AddHitstop(float seconds);

    GameData m_data;
    Rng m_rng;    // gameplay: spawn positions, strafe direction
    Rng m_fxRng;  // cosmetic: particles only
    PlayerState m_player;
    Pool<Enemy, kMaxEnemies> m_enemies;
    Pool<Projectile, kMaxProjectiles> m_projectiles;
    Pool<Particle, kMaxParticles> m_particles;
    std::array<Pillar, 4> m_pillars{};
    SpatialGrid m_grid;
    std::vector<SpatialGrid::Item> m_gridItems;
    std::vector<GameEvent> m_events;
    std::vector<int> m_spawnQueue;  // enemy def indices, front = next
    std::size_t m_spawnHead = 0;
    float m_maxEnemyRadius = 0.0f;

    int m_wave = 0;
    WavePhase m_phase = WavePhase::Intermission;
    float m_phaseTimer = 0.0f;
    float m_spawnTimer = 0.0f;
    float m_waveHpScale = 1.0f;
    bool m_wavesEnabled = true;

    int m_score = 0;
    int m_kills = 0;
    std::uint64_t m_tick = 0;
    std::uint32_t m_enemySerial = 0;
    std::uint32_t m_droppedEvents = 0;
    float m_hitstop = 0.0f;
};

}  // namespace arena
