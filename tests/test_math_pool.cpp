#include <doctest/doctest.h>

#include "arena/Math.h"
#include "arena/Pool.h"

using namespace arena;

TEST_CASE("Normalize returns zero, not NaN, for a zero vector") {
    const Vec2 n = Normalize({0.0f, 0.0f});
    CHECK(n.x == 0.0f);
    CHECK(n.y == 0.0f);
    CHECK(Length(Normalize({3.0f, 4.0f})) == doctest::Approx(1.0f));
}

TEST_CASE("MoveToward stops exactly on the target and never overshoots") {
    CHECK(MoveToward({0, 0}, {10, 0}, 3.0f) == Vec2{3, 0});
    CHECK(MoveToward({9, 0}, {10, 0}, 3.0f) == Vec2{10, 0});
}

TEST_CASE("InSector: inside, outside the arc, outside the range, and the target radius") {
    const Vec2 o{0, 0}, right{1, 0};
    const float half = 60.0f * kPi / 180.0f;  // 120 degree arc
    CHECK(InSector(o, right, 50, half, {40, 0}, 0));
    CHECK(InSector(o, right, 50, half, {20, 30}, 0));    // ~56 degrees off: inside
    CHECK_FALSE(InSector(o, right, 50, half, {0, 40}, 0));  // 90 degrees off
    CHECK_FALSE(InSector(o, right, 50, half, {-40, 0}, 0));
    CHECK_FALSE(InSector(o, right, 50, half, {60, 0}, 0));
    CHECK(InSector(o, right, 50, half, {60, 0}, 12));     // its edge reaches into range
    CHECK(InSector(o, right, 50, half, {-5, 0}, 12));     // overlapping the origin counts
}

TEST_CASE("Rng is deterministic per seed and stays in range") {
    Rng a(42), b(42), c(43);
    for (int i = 0; i < 100; ++i) {
        const float x = a.Next01();
        CHECK(x == b.Next01());
        CHECK(x >= 0.0f);
        CHECK(x < 1.0f);
    }
    CHECK(a.NextU64() != c.NextU64());
    Rng r(7);
    for (int i = 0; i < 1000; ++i) {
        const int v = r.RangeInt(2, 5);
        CHECK(v >= 2);
        CHECK(v <= 5);
    }
}

TEST_CASE("Pool: spawn, get, despawn, and full") {
    Pool<int, 3> pool;
    const Handle a = pool.Spawn(10);
    const Handle b = pool.Spawn(20);
    const Handle c = pool.Spawn(30);
    CHECK(pool.Full());
    CHECK_FALSE(pool.Spawn(40).IsValid());
    CHECK(*pool.Get(b) == 20);
    CHECK(pool.Despawn(b));
    CHECK(pool.Size() == 2);
    CHECK(pool.Get(b) == nullptr);
    CHECK_FALSE(pool.Despawn(b));  // double free is a no-op
    CHECK(*pool.Get(a) == 10);
    CHECK(*pool.Get(c) == 30);
}

TEST_CASE("Pool: a stale handle does not resolve to the slot's new occupant") {
    Pool<int, 2> pool;
    const Handle old = pool.Spawn(1);
    pool.Despawn(old);
    const Handle fresh = pool.Spawn(2);
    CHECK(fresh.index == old.index);  // same slot reused...
    CHECK(fresh.generation != old.generation);
    CHECK(pool.Get(old) == nullptr);  // ...but the old handle is dead
    CHECK(*pool.Get(fresh) == 2);
}

TEST_CASE("Pool: ForEach visits in index order and tolerates despawning the current item") {
    Pool<int, 8> pool;
    for (int i = 0; i < 6; ++i) pool.Spawn(i);
    int visited = 0;
    int last = -1;
    pool.ForEach([&](std::uint32_t idx, int& v) {
        CHECK(static_cast<int>(idx) > last);
        last = static_cast<int>(idx);
        if (v % 2 == 0) pool.DespawnIndex(idx);
        ++visited;
    });
    CHECK(visited == 6);
    CHECK(pool.Size() == 3);
}
