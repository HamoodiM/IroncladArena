#include "Autopilot.h"

#include <cmath>

namespace game {

using namespace arena;

PlayerInput Autopilot::Think(const World& world) {
    ++m_tick;
    PlayerInput in;
    const PlayerState& me = world.Player();
    const PlayerTuning& t = world.Data().player;
    in.aim = me.pos + me.facing * 100.0f;
    if (!me.alive) return in;

    // Nearest active enemy, and the most urgent threat.
    const Enemy* nearest = nullptr;
    float nearestDist = 1e9f;
    Vec2 threatDir;
    bool threatened = false;
    world.Enemies().ForEach([&](std::uint32_t, const Enemy& e) {
        if (e.state == EnemyState::Spawning) return;
        const EnemyDef& def = world.Data().enemies[e.def];
        const float d = Distance(e.pos, me.pos) - def.radius - t.radius;
        if (d < nearestDist) {
            nearestDist = d;
            nearest = &e;
        }
        const bool winding = e.state == EnemyState::Windup && e.stateTime > def.windup - 0.12f;
        if (!winding) return;
        if ((def.kind == EnemyKind::Melee && d < def.attackRange + 70.0f) ||
            (def.kind == EnemyKind::Brute && d < def.slamRadius + 10.0f)) {
            threatened = true;
            threatDir = Normalize(me.pos - e.pos);
        }
    });
    world.Projectiles().ForEach([&](std::uint32_t, const Projectile& p) {
        if (p.fromPlayer) return;
        const Vec2 to = me.pos - p.pos;
        if (LengthSq(to) < 110.0f * 110.0f && Dot(to, p.vel) > 0.0f) {
            threatened = true;
            threatDir = Normalize(Perp(p.vel)) * m_orbitSign;
        }
    });

    // Keep away from walls and pillars.
    Vec2 avoid;
    const Vec2 mid = (World::kArenaMin + World::kArenaMax) * 0.5f;
    if (me.pos.x < 120 || me.pos.x > World::kArenaMax.x - 120 || me.pos.y < 120 || me.pos.y > World::kArenaMax.y - 120)
        avoid += Normalize(mid - me.pos);
    for (const Pillar& p : world.Pillars())
        if (Distance(me.pos, p.pos) < p.radius + 60.0f) avoid += Normalize(me.pos - p.pos);

    if (threatened) {
        in.dashPressed = true;
        in.move = Normalize(Perp(threatDir) * m_orbitSign + threatDir * 0.6f + avoid);
        if ((m_tick % 240) == 0) m_orbitSign = -m_orbitSign;
        return in;
    }

    if (!nearest) {
        in.move = Normalize(mid - me.pos) * (Distance(mid, me.pos) > 80.0f ? 1.0f : 0.0f);
        return in;
    }

    in.aim = nearest->pos;
    const Vec2 toEnemy = Normalize(nearest->pos - me.pos);
    if (nearestDist < t.meleeRange * 0.8f) {
        // In reach: combo, and circle a little so we're not a sitting target.
        if (m_tick - m_lastSwingTick >= 9) {
            in.meleePressed = true;
            m_lastSwingTick = m_tick;
        }
        in.move = Normalize(Perp(toEnemy) * (0.5f * m_orbitSign) + avoid);
    } else if (nearestDist < 380.0f) {
        in.shootHeld = nearestDist > 150.0f;
        in.move = Normalize(toEnemy + Perp(toEnemy) * (0.3f * m_orbitSign) + avoid * 1.5f);
    } else {
        in.shootHeld = true;
        in.move = Normalize(toEnemy + avoid);
    }
    if ((m_tick % 300) == 0) m_orbitSign = -m_orbitSign;
    return in;
}

}  // namespace game
