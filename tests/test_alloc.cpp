// Proves the "no heap allocations during play" claim: counts every global
// operator new in this test binary while the world simulates three minutes of
// waves with a scripted player.
#include <doctest/doctest.h>

#include <atomic>
#include <cstdlib>
#include <memory>
#include <new>

#include "TestData.h"
#include "arena/World.h"

namespace {
std::atomic<long long> g_news{0};
}

void* operator new(std::size_t n) {
    g_news.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n) {
    g_news.fetch_add(1, std::memory_order_relaxed);
    if (void* p = std::malloc(n ? n : 1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace arena;

TEST_CASE("World::Step makes no heap allocations, across many waves") {
    const GameData data = LoadRealGameData();
    auto world = std::make_unique<World>(data, 2024);  // allocation here is fine: load time
    world->DebugPlayer().hurtInvuln = 1e9f;              // survive so the waves keep coming

    long long allocs = 0;
    for (int tick = 0; tick < 60 * 180; ++tick) {
        PlayerInput in;
        const float t = static_cast<float>(tick) * World::kStep;
        in.move = FromAngle(t * 0.7f);
        in.aim = world->Player().pos + FromAngle(t * 3.1f) * 100.0f;
        in.meleePressed = tick % 9 == 0;
        in.dashPressed = tick % 80 == 0;
        in.shootHeld = (tick / 90) % 2 == 0;

        const long long before = g_news.load();
        world->Step(in);
        allocs += g_news.load() - before;
    }
    INFO("reached wave " << world->Wave() << ", " << world->Kills() << " kills");
    CHECK(world->Wave() >= 3);
    CHECK(allocs == 0);
}
