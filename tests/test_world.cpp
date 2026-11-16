#include <doctest/doctest.h>

#include "TestData.h"
#include "arena/World.h"

using namespace arena;

namespace {

struct Arena {
    GameData data = LoadRealGameData();
    World world{data, 7};
    int grunt = data.FindEnemy("grunt");
    int spitter = data.FindEnemy("spitter");
    int brute = data.FindEnemy("brute");

    Arena() { world.DebugSetWavesEnabled(false); }

    Vec2 PlayerPos() const { return world.Player().pos; }

    PlayerInput Aim(Vec2 at) const {
        PlayerInput in;
        in.aim = at;
        return in;
    }

    int Count(EventType type) const {
        int n = 0;
        for (const auto& e : world.Events()) n += (e.type == type);
        return n;
    }

    // Steps (skipping hitstop freeze ticks) and counts an event over the run.
    int StepCounting(int ticks, EventType type, PlayerInput in = {}) {
        int n = 0;
        for (int i = 0; i < ticks; ++i) {
            world.Step(in);
            n += Count(type);
        }
        return n;
    }
};

}  // namespace

TEST_CASE("Melee hits an enemy in the arc and misses one behind the player") {
    Arena a;
    const Vec2 p = a.PlayerPos();
    const Handle front = a.world.DebugSpawnEnemy(a.grunt, p + Vec2{50, 0});
    const Handle back = a.world.DebugSpawnEnemy(a.grunt, p + Vec2{-50, 0});
    PlayerInput in = a.Aim(p + Vec2{100, 0});
    in.meleePressed = true;
    a.world.Step(in);
    CHECK(a.Count(EventType::EnemyHit) == 1);
    CHECK(a.world.DebugEnemy(front)->hp < a.data.enemies[a.grunt].maxHp);
    CHECK(a.world.DebugEnemy(back)->hp == a.data.enemies[a.grunt].maxHp);
}

TEST_CASE("A melee hit knocks back, staggers a grunt, and triggers hitstop") {
    Arena a;
    const Vec2 p = a.PlayerPos();
    const Handle h = a.world.DebugSpawnEnemy(a.grunt, p + Vec2{50, 0}, EnemyState::Windup);
    PlayerInput in = a.Aim(p + Vec2{100, 0});
    in.meleePressed = true;
    a.world.Step(in);
    const Enemy* e = a.world.DebugEnemy(h);
    REQUIRE(e);
    CHECK(e->state == EnemyState::Stagger);  // the hit cancelled its attack
    CHECK(e->vel.x > 0.0f);
    CHECK(a.world.Hitstop() > 0.0f);
}

TEST_CASE("Hitstop freezes the fight but keeps a button press") {
    Arena a;
    const Vec2 p = a.PlayerPos();
    const Handle h = a.world.DebugSpawnEnemy(a.grunt, p + Vec2{50, 0});
    PlayerInput swing = a.Aim(p + Vec2{100, 0});
    swing.meleePressed = true;
    a.world.Step(swing);
    REQUIRE(a.world.Hitstop() > 0.0f);

    const Vec2 frozen = a.world.DebugEnemy(h)->pos;
    a.world.Step(swing);  // press during the freeze
    CHECK(a.world.DebugEnemy(h)->pos == frozen);
    CHECK(a.world.Player().meleeBuffer > 0.0f);
}

TEST_CASE("Turning hitstop off removes the freeze") {
    Arena a;
    a.world.feel.hitstop = false;
    const Vec2 p = a.PlayerPos();
    a.world.DebugSpawnEnemy(a.grunt, p + Vec2{50, 0});
    PlayerInput in = a.Aim(p + Vec2{100, 0});
    in.meleePressed = true;
    a.world.Step(in);
    CHECK(a.world.Hitstop() == 0.0f);
}

TEST_CASE("The combo finisher kills a grunt the first two swings didn't") {
    Arena a;
    a.world.feel.hitstop = false;
    const Vec2 p = a.PlayerPos();
    a.world.DebugSpawnEnemy(a.grunt, p + Vec2{45, 0});
    PlayerInput mash = a.Aim(p + Vec2{100, 0});
    mash.meleePressed = true;
    const int kills = a.StepCounting(90, EventType::EnemyKilled, mash);
    CHECK(kills == 1);
    CHECK(a.world.Score() == a.data.enemies[a.grunt].score);
}

TEST_CASE("A grunt's lunge damages the player once; a dash through it avoids the hit") {
    {
        Arena a;
        const Vec2 p = a.PlayerPos();
        a.world.DebugSpawnEnemy(a.grunt, p + Vec2{70, 0}, EnemyState::Chase);
        const int hurts = a.StepCounting(90, EventType::PlayerHurt, a.Aim(p));
        CHECK(hurts >= 1);
        CHECK(a.world.Player().hp < a.data.player.maxHp);
    }
    {
        Arena a;
        const Vec2 p = a.PlayerPos();
        a.world.DebugSpawnEnemy(a.grunt, p + Vec2{70, 0}, EnemyState::Chase);
        int hurts = 0;
        // Dash the moment the grunt commits to its attack.
        for (int i = 0; i < 90; ++i) {
            PlayerInput in = a.Aim(p);
            in.move = {0, 1};
            for (const auto& ev : a.world.Events()) in.dashPressed |= (ev.type == EventType::EnemyAttack);
            a.world.Step(in);
            hurts += a.Count(EventType::PlayerHurt);
            if (i > 40) break;  // past the first attack
        }
        CHECK(hurts == 0);
    }
}

TEST_CASE("Spitter shots hurt; a dashing player lets them pass through") {
    Arena a;
    a.world.feel.hitstop = false;
    const Vec2 p = a.PlayerPos();
    a.world.DebugSpawnEnemy(a.spitter, p + Vec2{0, -300}, EnemyState::Windup);
    int hurts = a.StepCounting(120, EventType::PlayerHurt, a.Aim(p));
    CHECK(hurts == 1);

    Arena b;
    b.world.DebugSpawnEnemy(b.spitter, b.PlayerPos() + Vec2{0, -300}, EnemyState::Windup);
    b.world.DebugPlayer().iframes = 10.0f;  // as if dashing the whole time
    CHECK(b.StepCounting(120, EventType::PlayerHurt, b.Aim(b.PlayerPos())) == 0);
}

TEST_CASE("Brute slam hits everything in its radius") {
    Arena a;
    const Vec2 p = a.PlayerPos();
    a.world.DebugSpawnEnemy(a.brute, p + Vec2{60, 0}, EnemyState::Windup);
    int slams = 0, hurts = 0;
    for (int i = 0; i < 90; ++i) {
        a.world.Step(a.Aim(p));
        slams += a.Count(EventType::Slam);
        hurts += a.Count(EventType::PlayerHurt);
    }
    CHECK(slams == 1);
    CHECK(hurts == 1);
    CHECK(a.world.Player().hp == a.data.player.maxHp - static_cast<int>(a.data.enemies[a.brute].damage));
}

TEST_CASE("Invulnerability after a hit stops a second hit right away") {
    Arena a;
    const Vec2 p = a.PlayerPos();
    a.world.DebugSpawnEnemy(a.brute, p + Vec2{60, 0}, EnemyState::Windup);
    a.world.DebugSpawnEnemy(a.brute, p + Vec2{-60, 0}, EnemyState::Windup);
    CHECK(a.StepCounting(90, EventType::PlayerHurt, a.Aim(p)) == 1);
}

TEST_CASE("Enemies don't overlap each other or the pillars") {
    Arena a;
    a.world.feel.hitstop = false;
    const Vec2 p = a.PlayerPos();
    for (int i = 0; i < 20; ++i) a.world.DebugSpawnEnemy(a.grunt, p + Vec2{400.0f + (i % 5) * 2.0f, (i / 5) * 2.0f});
    a.world.DebugPlayer().hurtInvuln = 1000.0f;  // keep the player alive and still
    for (int i = 0; i < 240; ++i) a.world.Step(a.Aim(p));

    const float r = a.data.enemies[a.grunt].radius;
    int overlaps = 0;
    a.world.Enemies().ForEach([&](std::uint32_t i, const Enemy& e1) {
        a.world.Enemies().ForEach([&](std::uint32_t j, const Enemy& e2) {
            if (j <= i) return;
            if (Distance(e1.pos, e2.pos) < 2 * r * 0.75f) ++overlaps;  // allow soft contact
        });
        for (const auto& pillar : a.world.Pillars()) CHECK(Distance(e1.pos, pillar.pos) >= pillar.radius + r - 0.01f);
    });
    CHECK(overlaps == 0);
}

TEST_CASE("Projectiles stop at pillars and walls") {
    Arena a;
    const auto& pillar = a.world.Pillars()[0];
    a.world.DebugPlayer().pos = pillar.pos + Vec2{150, 0};
    PlayerInput in = a.Aim(pillar.pos);
    in.shootHeld = true;
    a.world.Step(in);
    REQUIRE(a.world.Projectiles().Size() == 1);
    CHECK(a.StepCounting(30, EventType::ProjectileHitWall, a.Aim(pillar.pos)) == 1);
    CHECK(a.world.Projectiles().Size() == 0);
}

TEST_CASE("Waves: the first wave spawns, gets cleared, and the next one starts") {
    GameData data = LoadRealGameData();
    World w(data, 3);
    w.DebugPlayer().hurtInvuln = 1e6f;  // can't die
    w.feel.hitstop = false;

    int started = 0, cleared = 0;
    for (int i = 0; i < 60 * 60 && cleared < 1; ++i) {
        // Bot: swing at whatever is nearest.
        PlayerInput in;
        Vec2 nearest = w.Player().pos + Vec2{1, 0};
        float best = 1e9f;
        w.Enemies().ForEach([&](std::uint32_t, const Enemy& e) {
            const float d = DistanceSq(e.pos, w.Player().pos);
            if (d < best && e.state != EnemyState::Spawning) {
                best = d;
                nearest = e.pos;
            }
        });
        in.aim = nearest;
        in.move = best > 50.0f * 50.0f ? Normalize(nearest - w.Player().pos) : Vec2{};
        in.meleePressed = (i % 6) == 0;
        w.Step(in);
        for (const auto& ev : w.Events()) {
            started += ev.type == EventType::WaveStarted;
            cleared += ev.type == EventType::WaveCleared;
        }
    }
    CHECK(started == 1);
    CHECK(cleared == 1);
    CHECK(w.Kills() == data.waves[0].groups[0].count);
}

TEST_CASE("No new spawns while the player is dead") {
    GameData data = LoadRealGameData();
    World w(data, 5);
    w.DebugPlayer().alive = false;
    for (int i = 0; i < 600; ++i) w.Step({});
    CHECK(w.Wave() == 0);
    CHECK(w.Enemies().Size() == 0);
}
