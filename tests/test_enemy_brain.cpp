#include <doctest/doctest.h>

#include "arena/EnemyBrain.h"

using namespace arena;

namespace {
constexpr float dt = 1.0f / 60.0f;

EnemyDef Grunt() {
    EnemyDef d;
    d.kind = EnemyKind::Melee;
    d.radius = 14;
    d.attackRange = 40;
    d.windup = 0.4f;
    d.active = 0.2f;
    d.recover = 0.5f;
    return d;
}

// Ticks until the state changes (or the limit), returns the new state.
EnemyState RunUntilChange(Enemy& e, const EnemyDef& d, const BrainContext& ctx, int limit, int* ticks = nullptr) {
    const EnemyState start = e.state;
    BrainOutput out;
    for (int i = 1; i <= limit; ++i) {
        UpdateEnemyBrain(e, d, ctx, dt, out);
        if (e.state != start) {
            if (ticks) *ticks = i;
            return e.state;
        }
    }
    if (ticks) *ticks = limit;
    return e.state;
}
}  // namespace

TEST_CASE("Spawning lasts spawnTime, then the enemy chases") {
    const EnemyDef d = Grunt();
    Enemy e;
    BrainContext ctx;
    ctx.playerPos = {500, 0};
    int ticks = 0;
    CHECK(RunUntilChange(e, d, ctx, 120, &ticks) == EnemyState::Chase);
    CHECK(ticks == doctest::Approx(ctx.spawnTime / dt).epsilon(0.05));
}

TEST_CASE("Melee: chases from far away, winds up in range, and locks its direction") {
    const EnemyDef d = Grunt();
    Enemy e;
    e.state = EnemyState::Chase;
    BrainContext ctx;
    ctx.playerPos = {300, 0};
    BrainOutput out;
    UpdateEnemyBrain(e, d, ctx, dt, out);
    CHECK(e.state == EnemyState::Chase);
    CHECK(out.desiredVel.x > 0.0f);

    ctx.playerPos = {d.radius + ctx.playerRadius + d.attackRange - 1, 0};
    UpdateEnemyBrain(e, d, ctx, dt, out);
    CHECK(e.state == EnemyState::Windup);
    CHECK(out.startedWindup);
    CHECK(e.attackDir.x == doctest::Approx(1.0f));

    // The player sidesteps during the windup: the lunge still goes the old way.
    ctx.playerPos = {0, 60};
    CHECK(RunUntilChange(e, d, ctx, 60) == EnemyState::Attack);
    CHECK(e.attackDir.x == doctest::Approx(1.0f));
    UpdateEnemyBrain(e, d, ctx, dt, out);
    CHECK(out.snapVelocity);
    CHECK(out.desiredVel.x == doctest::Approx(d.lungeSpeed));
}

TEST_CASE("Every attack has a windup and a recover window of the configured length") {
    const EnemyDef d = Grunt();
    Enemy e;
    e.state = EnemyState::Windup;
    BrainContext ctx;
    ctx.playerPos = {30, 0};
    int windupTicks = 0, activeTicks = 0, recoverTicks = 0;
    CHECK(RunUntilChange(e, d, ctx, 200, &windupTicks) == EnemyState::Attack);
    CHECK(RunUntilChange(e, d, ctx, 200, &activeTicks) == EnemyState::Recover);
    CHECK(RunUntilChange(e, d, ctx, 200, &recoverTicks) == EnemyState::Chase);
    CHECK(windupTicks * dt == doctest::Approx(d.windup).epsilon(0.05));
    CHECK(activeTicks * dt == doctest::Approx(d.active).epsilon(0.1));
    CHECK(recoverTicks * dt == doctest::Approx(d.recover).epsilon(0.05));
}

TEST_CASE("beginAttack fires exactly once per attack") {
    const EnemyDef d = Grunt();
    Enemy e;
    e.state = EnemyState::Windup;
    BrainContext ctx;
    ctx.playerPos = {30, 0};
    int begins = 0;
    BrainOutput out;
    for (int i = 0; i < 40; ++i) {
        UpdateEnemyBrain(e, d, ctx, dt, out);
        begins += out.beginAttack;
    }
    CHECK(begins == 1);
}

TEST_CASE("Stagger interrupts a windup and resets poise") {
    EnemyDef d = Grunt();
    d.poise = 3;
    d.staggerTime = 0.3f;
    Enemy e;
    e.state = EnemyState::Windup;
    e.poise = -1;
    StaggerEnemy(e, d);
    CHECK(e.state == EnemyState::Stagger);
    CHECK(e.poise == 3);
    BrainContext ctx;
    ctx.playerPos = {30, 0};
    int ticks = 0;
    CHECK(RunUntilChange(e, d, ctx, 100, &ticks) == EnemyState::Chase);
    CHECK(ticks * dt == doctest::Approx(d.staggerTime).epsilon(0.06));
}

TEST_CASE("Spawning enemies can't be staggered") {
    const EnemyDef d = Grunt();
    Enemy e;
    StaggerEnemy(e, d);
    CHECK(e.state == EnemyState::Spawning);
}

TEST_CASE("Ranged: backs off when too close, approaches when far, strafes in its band") {
    EnemyDef d;
    d.kind = EnemyKind::Ranged;
    d.preferredRange = 300;
    d.speed = 100;
    d.attackRange = 0;  // never fire in this test
    Enemy e;
    e.state = EnemyState::Chase;
    BrainContext ctx;
    BrainOutput out;

    ctx.playerPos = {100, 0};
    UpdateEnemyBrain(e, d, ctx, dt, out);
    CHECK(out.desiredVel.x < 0.0f);

    ctx.playerPos = {600, 0};
    UpdateEnemyBrain(e, d, ctx, dt, out);
    CHECK(out.desiredVel.x > 0.0f);

    ctx.playerPos = {300, 0};
    UpdateEnemyBrain(e, d, ctx, dt, out);
    CHECK(out.desiredVel.x == doctest::Approx(0.0f));
    CHECK(out.desiredVel.y != 0.0f);
}

TEST_CASE("Ranged: tracks during the windup, then stops aiming just before the shot") {
    EnemyDef d;
    d.kind = EnemyKind::Ranged;
    d.windup = 0.6f;
    Enemy e;
    e.state = EnemyState::Windup;
    e.attackDir = {1, 0};
    BrainContext ctx;
    ctx.playerPos = {0, 300};  // straight down
    BrainOutput out;
    UpdateEnemyBrain(e, d, ctx, dt, out);
    CHECK(e.attackDir.y == doctest::Approx(1.0f));  // tracking

    for (int i = 0; i < 30; ++i) UpdateEnemyBrain(e, d, ctx, dt, out);  // now in the last 0.18 s
    ctx.playerPos = {300, 0};
    UpdateEnemyBrain(e, d, ctx, dt, out);
    CHECK(e.attackDir.y == doctest::Approx(1.0f));  // locked: a late sidestep dodges
}

TEST_CASE("When the player dies, enemies go idle") {
    const EnemyDef d = Grunt();
    Enemy e;
    e.state = EnemyState::Chase;
    BrainContext ctx;
    ctx.playerAlive = false;
    BrainOutput out;
    UpdateEnemyBrain(e, d, ctx, dt, out);
    CHECK(e.state == EnemyState::Idle);
    CHECK(out.desiredVel == Vec2{});
}
