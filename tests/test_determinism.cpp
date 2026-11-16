#include <doctest/doctest.h>

#include <vector>

#include "TestData.h"
#include "arena/World.h"

using namespace arena;

namespace {

// A scripted, input-only "player": circles the arena centre, swings, shoots and
// dashes on fixed ticks. Because it depends only on the tick number, two runs
// get identical inputs.
PlayerInput Scripted(std::uint64_t tick) {
    PlayerInput in;
    const float t = static_cast<float>(tick) * World::kStep;
    in.move = FromAngle(t * 0.9f);
    in.aim = Vec2{800, 480} + FromAngle(t * 2.3f) * 200.0f;
    in.meleePressed = (tick % 17) == 0;
    in.dashPressed = (tick % 97) == 0;
    in.shootHeld = (tick / 120) % 2 == 1;
    return in;
}

std::vector<std::uint64_t> Run(const GameData& data, std::uint64_t seed, int ticks) {
    World w(data, seed);
    w.DebugPlayer().hurtInvuln = 1e6f;  // survive the whole run so all systems stay busy
    std::vector<std::uint64_t> hashes;
    for (int i = 0; i < ticks; ++i) {
        w.Step(Scripted(w.Tick()));
        if (i % 60 == 0) hashes.push_back(w.StateHash());
    }
    hashes.push_back(w.StateHash());
    return hashes;
}

}  // namespace

TEST_CASE("Same seed + same inputs = identical simulation (90 s of play)") {
    const GameData data = LoadRealGameData();
    const auto a = Run(data, 99, 60 * 90);
    const auto b = Run(data, 99, 60 * 90);
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        INFO("diverged at second " << i);
        REQUIRE(a[i] == b[i]);
    }
}

TEST_CASE("A different seed gives a different fight") {
    const GameData data = LoadRealGameData();
    CHECK(Run(data, 1, 60 * 20).back() != Run(data, 2, 60 * 20).back());
}

TEST_CASE("Particles don't change gameplay: cosmetic randomness has its own generator") {
    // World b starts with its particle pool full, so every Burst() gives up
    // early and draws far fewer random numbers than in world a. If particles
    // shared the gameplay generator, spawn positions would drift apart.
    const GameData data = LoadRealGameData();
    World a(data, 11), b(data, 11);
    b.DebugFillParticles();
    a.DebugPlayer().hurtInvuln = b.DebugPlayer().hurtInvuln = 1e6f;
    for (int i = 0; i < 60 * 30; ++i) {
        const PlayerInput in = Scripted(a.Tick());
        a.Step(in);
        b.Step(in);
    }
    CHECK(a.Particles().Size() < b.Particles().Size());
    CHECK(a.StateHash() == b.StateHash());
    CHECK(a.Wave() >= 1);
}
