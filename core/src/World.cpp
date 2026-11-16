#include "arena/World.h"

#include <algorithm>
#include <cmath>

namespace arena {
namespace {

constexpr float kSpawnTime = 0.6f;        // enemies fade in (and can't act or be hit) for this long
constexpr float kMinSpawnDistance = 360.0f;
constexpr float kAvoidDistance = 50.0f;   // pillar steering starts this close
constexpr float kSeparationStrength = 12.0f;
constexpr std::size_t kMaxWaveGroups = 16;

constexpr std::uint32_t kSparkColor = 0xFFE9A8FFu;
constexpr std::uint32_t kPlayerColor = 0x7FD6FFFFu;
constexpr std::uint32_t kHurtColor = 0xFF5050FFu;
constexpr std::uint32_t kDustColor = 0xC8B89AFFu;

void CountDown(float& timer, float dt) { timer = timer > dt ? timer - dt : 0.0f; }

struct Fnv {
    std::uint64_t h = 1469598103934665603ull;
    void Bytes(const void* data, std::size_t n) {
        const auto* p = static_cast<const unsigned char*>(data);
        for (std::size_t i = 0; i < n; ++i) {
            h ^= p[i];
            h *= 1099511628211ull;
        }
    }
    template <class T>
    void Add(const T& v) {
        Bytes(&v, sizeof(T));
    }
    void Add(Vec2 v) {
        Add(v.x);
        Add(v.y);
    }
};

}  // namespace

World::World(const GameData& data, std::uint64_t seed) : m_data(data) {
    for (const auto& def : m_data.enemies) m_maxEnemyRadius = std::max(m_maxEnemyRadius, def.radius);
    m_grid.Configure(kArenaMin, kArenaMax, 64.0f, static_cast<std::uint32_t>(kMaxEnemies));
    m_gridItems.resize(kMaxEnemies);
    m_events.reserve(kMaxEvents);
    m_spawnQueue.reserve(kMaxSpawnQueue);
    const Vec2 c = (kArenaMin + kArenaMax) * 0.5f;
    m_pillars = {Pillar{c + Vec2{-330.0f, -190.0f}, 46.0f}, Pillar{c + Vec2{330.0f, -190.0f}, 46.0f},
                 Pillar{c + Vec2{-330.0f, 190.0f}, 46.0f}, Pillar{c + Vec2{330.0f, 190.0f}, 46.0f}};
    Reset(seed);
}

void World::Reset(std::uint64_t seed) {
    m_rng = Rng(seed);
    m_fxRng = Rng(seed ^ 0xF00DF00Dull);
    ResetPlayer(m_player, m_data.player, (kArenaMin + kArenaMax) * 0.5f);
    m_enemies.Clear();
    m_projectiles.Clear();
    m_particles.Clear();
    m_events.clear();
    m_spawnQueue.clear();
    m_spawnHead = 0;
    m_wave = 0;
    m_phase = WavePhase::Intermission;
    m_phaseTimer = 1.2f;
    m_spawnTimer = 0.0f;
    m_waveHpScale = 1.0f;
    m_score = 0;
    m_kills = 0;
    m_tick = 0;
    m_enemySerial = 0;
    m_droppedEvents = 0;
    m_hitstop = 0.0f;
}

void World::Emit(EventType type, Vec2 pos, Vec2 dir, float value, int enemyDef) {
    if (m_events.size() >= kMaxEvents) {
        ++m_droppedEvents;
        return;
    }
    m_events.push_back({type, pos, dir, value, enemyDef});
}

void World::AddHitstop(float seconds) {
    if (feel.hitstop) m_hitstop = std::max(m_hitstop, seconds);
}

void World::Step(const PlayerInput& input) {
    const float dt = kStep;
    m_events.clear();
    ++m_tick;

    m_player.prevPos = m_player.pos;
    m_enemies.ForEach([](std::uint32_t, Enemy& e) { e.prevPos = e.pos; });
    m_projectiles.ForEach([](std::uint32_t, Projectile& p) { p.prevPos = p.pos; });
    m_particles.ForEach([](std::uint32_t, Particle& p) { p.prevPos = p.pos; });

    // Hitstop: the whole fight freezes for a few ticks on a heavy hit, but
    // button presses still land in the input buffer.
    if (m_hitstop > 0.0f) {
        BufferPlayerPresses(m_player, m_data.player, feel, input);
        CountDown(m_hitstop, dt);
        return;
    }

    PlayerActions actions;
    UpdatePlayerMotor(m_player, m_data.player, feel, input, dt, actions);
    BuildEnemyGrid();
    ApplyPlayerActions(actions);

    if (m_player.alive) {
        m_player.pos += m_player.vel * dt;
        ResolveStatic(m_player.pos, m_player.vel, m_data.player.radius);
        if (m_player.Dashing() && (m_tick % 2) == 0)
            Burst(m_player.pos, 1, kPlayerColor, 10.0f, 40.0f, 0.25f, 5.0f);
    }

    if (m_wavesEnabled && m_player.alive) UpdateWaves(dt);
    UpdateEnemies(dt);
    BuildEnemyGrid();
    ResolveEnemyOverlaps();
    BuildEnemyGrid();  // projectiles test against where enemies are now
    UpdateProjectiles(dt);

    m_enemies.ForEach([&](std::uint32_t i, Enemy& e) {
        if (e.hp > 0.0f) return;
        const EnemyDef& def = m_data.enemies[e.def];
        Emit(EventType::EnemyKilled, e.pos, {}, static_cast<float>(def.score), e.def);
        m_score += def.score;
        ++m_kills;
        Burst(e.pos, 22, def.color, 80.0f, 420.0f, 0.6f, 4.5f);
        AddHitstop(0.06f);
        m_enemies.DespawnIndex(i);
    });

    UpdateParticles(dt);
}

void World::BuildEnemyGrid() {
    std::uint32_t count = 0;
    m_enemies.ForEach([&](std::uint32_t i, const Enemy& e) {
        if (e.state == EnemyState::Spawning || e.hp <= 0.0f) return;
        m_gridItems[count++] = {i, e.pos};
    });
    m_grid.Build(m_gridItems.data(), count);
}

void World::ApplyPlayerActions(const PlayerActions& a) {
    const PlayerTuning& t = m_data.player;

    if (a.dashed) {
        Emit(EventType::PlayerDash, m_player.pos, m_player.dashDir);
        Burst(m_player.pos, 8, kDustColor, 60.0f, 160.0f, 0.35f, 4.0f, -m_player.dashDir, 0.7f);
    }

    if (a.swing >= 0) {
        Emit(EventType::PlayerSwing, m_player.pos, a.swingDir, static_cast<float>(a.swing));
        const float halfArc = t.meleeArcDeg * kPi / 360.0f;
        int hits = 0;
        m_grid.ForEachCandidate(m_player.pos, t.meleeRange + m_maxEnemyRadius, [&](std::uint32_t id, Vec2) {
            Enemy& e = m_enemies.At(id);
            const EnemyDef& def = m_data.enemies[e.def];
            if (e.hp <= 0.0f) return;
            if (!InSector(m_player.pos, a.swingDir, t.meleeRange, halfArc, e.pos, def.radius)) return;
            const Vec2 away = Normalize(e.pos - m_player.pos);
            DamageEnemy(id, t.meleeDamage[a.swing], LengthSq(away) > 0.0f ? away : a.swingDir, t.meleeKnockback[a.swing]);
            ++hits;
        });
        if (hits > 0) AddHitstop(a.swing == 2 ? 0.09f : 0.05f);
    }

    if (a.shot) {
        Projectile p;
        p.pos = p.prevPos = m_player.pos + a.shotDir * (t.radius + 6.0f);
        p.vel = a.shotDir * t.shotSpeed;
        p.radius = t.shotRadius;
        p.damage = t.shotDamage;
        p.knockback = t.shotKnockback;
        p.life = 1.5f;
        p.fromPlayer = true;
        m_projectiles.Spawn(p);
        Emit(EventType::PlayerShot, p.pos, a.shotDir);
    }
}

void World::DamageEnemy(std::uint32_t index, float damage, Vec2 dir, float knockback) {
    Enemy& e = m_enemies.At(index);
    const EnemyDef& def = m_data.enemies[e.def];
    e.hp -= damage;
    e.hitFlash = 0.12f;
    e.vel += dir * (knockback * def.knockbackScale);
    e.poise -= damage;
    Emit(EventType::EnemyHit, e.pos, dir, damage, e.def);
    Burst(e.pos, 6, kSparkColor, 120.0f, 380.0f, 0.25f, 3.0f, dir, 0.6f);
    if (e.poise <= 0.0f && e.hp > 0.0f) {
        StaggerEnemy(e, def);
        Emit(EventType::EnemyStaggered, e.pos, dir, 0.0f, e.def);
    }
}

bool World::DamagePlayer(float damage, Vec2 dir, float knockback) {
    if (!m_player.alive || m_player.Invulnerable()) return false;
    const int amount = std::max(1, static_cast<int>(std::lround(damage)));
    m_player.hp -= amount;
    m_player.hurtInvuln = m_data.player.hurtInvuln;
    m_player.vel += dir * knockback;
    Emit(EventType::PlayerHurt, m_player.pos, dir, static_cast<float>(amount));
    Burst(m_player.pos, 14, kHurtColor, 100.0f, 340.0f, 0.4f, 4.0f);
    AddHitstop(0.10f);
    if (m_player.hp <= 0) {
        m_player.hp = 0;
        m_player.alive = false;
        Emit(EventType::PlayerDied, m_player.pos);
        Burst(m_player.pos, 40, kPlayerColor, 60.0f, 500.0f, 1.0f, 5.0f);
    }
    return true;
}

void World::UpdateWaves(float dt) {
    switch (m_phase) {
        case WavePhase::Intermission:
            m_phaseTimer -= dt;
            if (m_phaseTimer <= 0.0f) StartWave();
            break;
        case WavePhase::Spawning: {
            const float interval = m_data.waves[std::min<std::size_t>(m_wave - 1, m_data.waves.size() - 1)].spawnInterval;
            m_spawnTimer -= dt;
            while (m_spawnTimer <= 0.0f && m_spawnHead < m_spawnQueue.size()) {
                if (!TrySpawnFromQueue()) {  // pool full: try again shortly
                    m_spawnTimer = 0.1f;
                    break;
                }
                m_spawnTimer += interval;
            }
            if (m_spawnHead >= m_spawnQueue.size()) m_phase = WavePhase::Fighting;
            break;
        }
        case WavePhase::Fighting:
            if (m_enemies.Size() == 0) {
                Emit(EventType::WaveCleared, m_player.pos, {}, static_cast<float>(m_wave));
                m_phase = WavePhase::Intermission;
                m_phaseTimer = m_data.intermission;
            }
            break;
    }
}

void World::StartWave() {
    ++m_wave;
    const std::size_t defined = m_data.waves.size();
    const std::size_t index = std::min<std::size_t>(static_cast<std::size_t>(m_wave - 1), defined - 1);
    const int extra = std::max(0, m_wave - static_cast<int>(defined));  // repeats past the last wave
    const WaveDef& wave = m_data.waves[index];
    m_waveHpScale = std::pow(m_data.extraWaveHpScale, static_cast<float>(extra));

    // Interleave the groups (A B C A B C ...) so a wave arrives mixed.
    std::array<int, kMaxWaveGroups> remaining{};
    const std::size_t groups = std::min(wave.groups.size(), kMaxWaveGroups);
    for (std::size_t g = 0; g < groups; ++g) remaining[g] = wave.groups[g].count + extra * m_data.extraWaveCount;
    m_spawnQueue.clear();
    m_spawnHead = 0;
    bool any = true;
    while (any && m_spawnQueue.size() < kMaxSpawnQueue) {
        any = false;
        for (std::size_t g = 0; g < groups && m_spawnQueue.size() < kMaxSpawnQueue; ++g) {
            if (remaining[g] <= 0) continue;
            m_spawnQueue.push_back(wave.groups[g].enemy);
            --remaining[g];
            any = true;
        }
    }
    m_phase = WavePhase::Spawning;
    m_spawnTimer = 0.0f;
    Emit(EventType::WaveStarted, m_player.pos, {}, static_cast<float>(m_wave));
}

bool World::TrySpawnFromQueue() {
    if (m_enemies.Full()) return false;
    const int defIndex = m_spawnQueue[m_spawnHead];
    const EnemyDef& def = m_data.enemies[defIndex];

    // A random point in a band along the walls, away from the player and
    // clear of the pillars.
    constexpr float margin = 70.0f;
    Vec2 pos;
    for (int attempt = 0; attempt < 24; ++attempt) {
        const float t = m_rng.Next01();
        switch (m_rng.RangeInt(0, 3)) {
            case 0: pos = {Lerp(kArenaMin.x + margin, kArenaMax.x - margin, t), kArenaMin.y + margin}; break;
            case 1: pos = {Lerp(kArenaMin.x + margin, kArenaMax.x - margin, t), kArenaMax.y - margin}; break;
            case 2: pos = {kArenaMin.x + margin, Lerp(kArenaMin.y + margin, kArenaMax.y - margin, t)}; break;
            default: pos = {kArenaMax.x - margin, Lerp(kArenaMin.y + margin, kArenaMax.y - margin, t)}; break;
        }
        if (Distance(pos, m_player.pos) >= kMinSpawnDistance && !HitsStatic(pos, def.radius + 10.0f)) break;
    }

    Enemy e;
    e.def = static_cast<std::uint16_t>(defIndex);
    e.state = EnemyState::Spawning;
    e.pos = e.prevPos = pos;
    e.hp = def.maxHp * m_waveHpScale;
    e.poise = def.poise;
    e.strafeSign = m_rng.Next01() < 0.5f ? -1.0f : 1.0f;
    e.serial = ++m_enemySerial;
    m_enemies.Spawn(e);
    ++m_spawnHead;
    Emit(EventType::EnemySpawned, pos, {}, 0.0f, defIndex);
    return true;
}

int World::EnemiesRemaining() const {
    return static_cast<int>(m_enemies.Size() + (m_spawnQueue.size() - m_spawnHead));
}

void World::UpdateEnemies(float dt) {
    BrainContext ctx;
    ctx.playerPos = m_player.pos;
    ctx.playerRadius = m_data.player.radius;
    ctx.playerAlive = m_player.alive;
    ctx.spawnTime = kSpawnTime;
    const float playerRadius = m_data.player.radius;

    m_enemies.ForEach([&](std::uint32_t i, Enemy& e) {
        if (e.hp <= 0.0f) return;
        const EnemyDef& def = m_data.enemies[e.def];
        CountDown(e.hitFlash, dt);

        BrainOutput out;
        UpdateEnemyBrain(e, def, ctx, dt, out);
        if (out.startedWindup) Emit(EventType::EnemyWindup, e.pos, e.attackDir, def.windup, e.def);
        if (out.beginAttack) {
            Emit(EventType::EnemyAttack, e.pos, e.attackDir, 0.0f, e.def);
            if (def.kind == EnemyKind::Ranged) {
                Projectile p;
                p.pos = p.prevPos = e.pos + e.attackDir * (def.radius + 8.0f);
                p.vel = e.attackDir * def.projectileSpeed;
                p.radius = 7.0f;
                p.damage = def.damage;
                p.knockback = 200.0f;
                p.life = 3.0f;
                p.fromPlayer = false;
                m_projectiles.Spawn(p);
            } else if (def.kind == EnemyKind::Brute) {
                const Vec2 center = e.pos + e.attackDir * (def.radius * 0.6f);
                Emit(EventType::Slam, center, e.attackDir, def.slamRadius, e.def);
                Burst(center, 26, kDustColor, 120.0f, 420.0f, 0.5f, 5.0f);
                if (m_player.alive && Distance(center, m_player.pos) <= def.slamRadius + playerRadius) {
                    const Vec2 away = Normalize(m_player.pos - center);
                    DamagePlayer(def.damage, LengthSq(away) > 0.0f ? away : e.attackDir, 520.0f);
                }
            }
        }

        if (e.state == EnemyState::Spawning) {
            e.vel = {};
            return;
        }

        Vec2 desired = out.desiredVel;
        if (!out.snapVelocity && e.state == EnemyState::Chase) {
            // Separation: push away from overlapping neighbours so a crowd
            // spreads around the player instead of stacking into one blob.
            m_grid.ForEachCandidate(e.pos, def.radius + m_maxEnemyRadius + 4.0f, [&](std::uint32_t id, Vec2 other) {
                if (id == i) return;
                const float minDist = def.radius + m_data.enemies[m_enemies.At(id).def].radius + 4.0f;
                const Vec2 d = e.pos - other;
                const float distSq = LengthSq(d);
                if (distSq >= minDist * minDist || distSq < 1e-6f) return;
                const float dist = std::sqrt(distSq);
                desired += d * ((minDist - dist) * kSeparationStrength / dist);
            });
            // Pillars: slide the part of the velocity that points into a
            // nearby pillar around it instead.
            for (const Pillar& p : m_pillars) {
                const Vec2 to = e.pos - p.pos;
                const float dist = Length(to);
                const float clearance = dist - p.radius - def.radius;
                if (clearance >= kAvoidDistance || dist < 1e-4f) continue;
                const Vec2 n = to / dist;
                const float inward = Dot(desired, n);
                if (inward >= 0.0f) continue;
                Vec2 tangent = Perp(n);
                if (Dot(tangent, desired) < 0.0f) tangent = -tangent;
                const float w = 1.0f - Clamp(clearance / kAvoidDistance, 0.0f, 1.0f);
                desired = desired - n * (inward * w) + tangent * (-inward * w);
            }
        }

        e.vel = out.snapVelocity ? desired : MoveToward(e.vel, desired, def.accel * dt);
        e.pos += e.vel * dt;
        ResolveStatic(e.pos, e.vel, def.radius);

        if (m_player.alive) {
            // Enemies never stand inside the player.
            const Vec2 d = e.pos - m_player.pos;
            const float minDist = def.radius + playerRadius;
            const float distSq = LengthSq(d);
            if (distSq < minDist * minDist) {
                const float dist = std::sqrt(distSq);
                const Vec2 n = dist > 1e-4f ? d / dist : -e.attackDir;
                e.pos = m_player.pos + n * minDist;
                ResolveStatic(e.pos, e.vel, def.radius);
            }
            // A lunge that reaches the player connects once. Dashing through
            // it still uses it up, so a well-timed dodge is a real dodge.
            if (def.kind == EnemyKind::Melee && e.state == EnemyState::Attack && !e.attackLanded &&
                Distance(e.pos, m_player.pos) <= minDist + 6.0f) {
                e.attackLanded = true;
                DamagePlayer(def.damage, e.attackDir, 380.0f);
            }
        }
    });
}

void World::ResolveEnemyOverlaps() {
    // Steering keeps a chasing crowd apart, but enemies that are winding up,
    // attacking or staggered don't steer. This pass separates any overlapping
    // pair directly, splitting the push by size so a brute shoves a grunt
    // more than the other way round.
    m_enemies.ForEach([&](std::uint32_t i, Enemy& a) {
        if (a.state == EnemyState::Spawning || a.hp <= 0.0f) return;
        const float ra = m_data.enemies[a.def].radius;
        m_grid.ForEachCandidate(a.pos, ra + m_maxEnemyRadius + 4.0f, [&](std::uint32_t id, Vec2) {
            if (id <= i) return;  // each pair once
            Enemy& b = m_enemies.At(id);
            const float rb = m_data.enemies[b.def].radius;
            const float minDist = ra + rb;
            const Vec2 d = b.pos - a.pos;
            const float distSq = LengthSq(d);
            if (distSq >= minDist * minDist) return;
            const float dist = std::sqrt(distSq);
            const Vec2 n = dist > 1e-4f ? d / dist : Vec2{1.0f, 0.0f};
            const float overlap = minDist - dist;
            const float wa = rb * rb / (ra * ra + rb * rb);  // share of the push a takes
            a.pos -= n * (overlap * wa);
            b.pos += n * (overlap * (1.0f - wa));
        });
    });
    m_enemies.ForEach([&](std::uint32_t, Enemy& e) {
        if (e.state != EnemyState::Spawning) ResolveStatic(e.pos, e.vel, m_data.enemies[e.def].radius);
    });
}

void World::UpdateProjectiles(float dt) {
    const float playerRadius = m_data.player.radius;
    m_projectiles.ForEach([&](std::uint32_t i, Projectile& p) {
        p.pos += p.vel * dt;
        p.life -= dt;
        if (p.life <= 0.0f) {
            m_projectiles.DespawnIndex(i);
            return;
        }
        if (HitsStatic(p.pos, p.radius)) {
            Emit(EventType::ProjectileHitWall, p.pos, Normalize(p.vel));
            Burst(p.pos, 5, p.fromPlayer ? kPlayerColor : 0xFFB070FFu, 40.0f, 160.0f, 0.2f, 3.0f, -Normalize(p.vel), 1.1f);
            m_projectiles.DespawnIndex(i);
            return;
        }
        if (p.fromPlayer) {
            bool hit = false;
            m_grid.ForEachCandidate(p.pos, p.radius + m_maxEnemyRadius, [&](std::uint32_t id, Vec2) {
                if (hit) return;
                Enemy& e = m_enemies.At(id);
                if (e.hp <= 0.0f) return;
                const float r = m_data.enemies[e.def].radius + p.radius;
                if (DistanceSq(e.pos, p.pos) > r * r) return;
                DamageEnemy(id, p.damage, Normalize(p.vel), p.knockback);
                hit = true;
            });
            if (hit) m_projectiles.DespawnIndex(i);
        } else if (m_player.alive) {
            const float r = playerRadius + p.radius;
            // An invulnerable (dashing) player lets shots pass through.
            if (DistanceSq(m_player.pos, p.pos) <= r * r && DamagePlayer(p.damage, Normalize(p.vel), p.knockback))
                m_projectiles.DespawnIndex(i);
        }
    });
}

void World::UpdateParticles(float dt) {
    m_particles.ForEach([&](std::uint32_t i, Particle& p) {
        p.life -= dt;
        if (p.life <= 0.0f) {
            m_particles.DespawnIndex(i);
            return;
        }
        p.pos += p.vel * dt;
        p.vel *= std::max(0.0f, 1.0f - p.drag * dt);
    });
}

void World::Burst(Vec2 pos, int count, std::uint32_t color, float speedMin, float speedMax, float life, float size,
                  Vec2 dir, float spread) {
    // Cosmetic randomness uses its own generator, so adding or removing a
    // spark never changes where the next enemy spawns.
    const bool directed = LengthSq(dir) > 0.0f;
    const float base = directed ? Angle(dir) : 0.0f;
    for (int n = 0; n < count; ++n) {
        Particle p;
        const float angle = directed ? base + m_fxRng.Range(-spread, spread) : m_fxRng.Range(0.0f, 2.0f * kPi);
        p.pos = p.prevPos = pos;
        p.vel = FromAngle(angle) * m_fxRng.Range(speedMin, speedMax);
        p.maxLife = life;
        p.life = life * m_fxRng.Range(0.6f, 1.0f);
        p.size = size * m_fxRng.Range(0.7f, 1.2f);
        p.color = color;
        if (!m_particles.Spawn(p).IsValid()) return;  // pool full: drop the rest
    }
}

void World::ResolveStatic(Vec2& pos, Vec2& vel, float radius) const {
    if (pos.x < kArenaMin.x + radius) { pos.x = kArenaMin.x + radius; vel.x = std::max(vel.x, 0.0f); }
    if (pos.x > kArenaMax.x - radius) { pos.x = kArenaMax.x - radius; vel.x = std::min(vel.x, 0.0f); }
    if (pos.y < kArenaMin.y + radius) { pos.y = kArenaMin.y + radius; vel.y = std::max(vel.y, 0.0f); }
    if (pos.y > kArenaMax.y - radius) { pos.y = kArenaMax.y - radius; vel.y = std::min(vel.y, 0.0f); }
    for (const Pillar& p : m_pillars) {
        const Vec2 to = pos - p.pos;
        const float minDist = p.radius + radius;
        const float distSq = LengthSq(to);
        if (distSq >= minDist * minDist) continue;
        const float dist = std::sqrt(distSq);
        const Vec2 n = dist > 1e-4f ? to / dist : Vec2{1.0f, 0.0f};
        pos = p.pos + n * minDist;
        const float vn = Dot(vel, n);
        if (vn < 0.0f) vel -= n * vn;
    }
}

bool World::HitsStatic(Vec2 pos, float radius) const {
    if (pos.x < kArenaMin.x + radius || pos.x > kArenaMax.x - radius || pos.y < kArenaMin.y + radius ||
        pos.y > kArenaMax.y - radius)
        return true;
    for (const Pillar& p : m_pillars)
        if (DistanceSq(pos, p.pos) < (p.radius + radius) * (p.radius + radius)) return true;
    return false;
}

Handle World::DebugSpawnEnemy(int def, Vec2 pos, EnemyState state) {
    const EnemyDef& d = m_data.enemies[def];
    Enemy e;
    e.def = static_cast<std::uint16_t>(def);
    e.state = state;
    e.pos = e.prevPos = pos;
    e.hp = d.maxHp;
    e.poise = d.poise;
    e.serial = ++m_enemySerial;
    return m_enemies.Spawn(e);
}

void World::DebugFillParticles() {
    Particle p;
    p.life = p.maxLife = 1e9f;
    p.drag = 0.0f;
    while (m_particles.Spawn(p).IsValid()) {
    }
}

std::uint64_t World::StateHash() const {
    Fnv f;
    f.Add(m_tick);
    f.Add(m_player.pos);
    f.Add(m_player.vel);
    f.Add(m_player.hp);
    f.Add(m_player.alive);
    f.Add(m_player.dashCooldown);
    f.Add(m_player.comboStep);
    m_enemies.ForEach([&](std::uint32_t i, const Enemy& e) {
        f.Add(i);
        f.Add(e.def);
        f.Add(e.state);
        f.Add(e.pos);
        f.Add(e.vel);
        f.Add(e.hp);
    });
    m_projectiles.ForEach([&](std::uint32_t i, const Projectile& p) {
        f.Add(i);
        f.Add(p.pos);
        f.Add(p.vel);
    });
    f.Add(m_score);
    f.Add(m_wave);
    f.Add(m_phase);
    f.Add(m_rng.state);
    f.Add(m_hitstop);
    return f.h;
}

}  // namespace arena
