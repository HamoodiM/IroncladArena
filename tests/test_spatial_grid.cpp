#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

#include "arena/SpatialGrid.h"

using namespace arena;

TEST_CASE("Grid radius queries match brute force on random points") {
    Rng rng(1234);
    std::vector<SpatialGrid::Item> items;
    for (std::uint32_t i = 0; i < 800; ++i) items.push_back({i, {rng.Range(0, 1600), rng.Range(0, 960)}});

    SpatialGrid grid;
    grid.Configure({0, 0}, {1600, 960}, 64.0f, 1000);
    grid.Build(items.data(), static_cast<std::uint32_t>(items.size()));
    CHECK(grid.Size() == 800);

    for (int q = 0; q < 200; ++q) {
        const Vec2 c{rng.Range(-50, 1650), rng.Range(-50, 1010)};
        const float r = rng.Range(5, 200);
        std::vector<std::uint32_t> got, want;
        grid.ForEachInRadius(c, r, [&](std::uint32_t id, Vec2) { got.push_back(id); });
        for (const auto& it : items)
            if (DistanceSq(it.pos, c) <= r * r) want.push_back(it.id);
        std::sort(got.begin(), got.end());
        CHECK(got == want);
    }
}

TEST_CASE("Grid keeps insertion order within a cell, so queries are deterministic") {
    SpatialGrid grid;
    grid.Configure({0, 0}, {100, 100}, 50.0f, 8);
    const SpatialGrid::Item items[] = {{7, {10, 10}}, {3, {20, 20}}, {9, {30, 30}}};
    grid.Build(items, 3);
    std::vector<std::uint32_t> order;
    grid.ForEachCandidate({25, 25}, 1.0f, [&](std::uint32_t id, Vec2) { order.push_back(id); });
    CHECK(order == std::vector<std::uint32_t>{7, 3, 9});
}

TEST_CASE("Grid clamps out-of-bounds points to edge cells instead of losing them") {
    SpatialGrid grid;
    grid.Configure({0, 0}, {100, 100}, 25.0f, 4);
    const SpatialGrid::Item items[] = {{1, {-30, -30}}, {2, {500, 50}}};
    grid.Build(items, 2);
    CHECK(grid.CellCount(0, 0) == 1);
    CHECK(grid.CellCount(3, 2) == 1);
    int found = 0;
    grid.ForEachInRadius({-30, -30}, 1.0f, [&](std::uint32_t id, Vec2) { found += (id == 1); });
    CHECK(found == 1);
}

TEST_CASE("Grid reports overflow instead of writing past capacity") {
    SpatialGrid grid;
    grid.Configure({0, 0}, {100, 100}, 50.0f, 2);
    const SpatialGrid::Item items[] = {{1, {1, 1}}, {2, {2, 2}}, {3, {3, 3}}};
    grid.Build(items, 3);
    CHECK(grid.Size() == 2);
    CHECK(grid.Overflow() == 1);
}

TEST_CASE("Rebuilding replaces the old contents") {
    SpatialGrid grid;
    grid.Configure({0, 0}, {100, 100}, 50.0f, 4);
    const SpatialGrid::Item first[] = {{1, {10, 10}}};
    const SpatialGrid::Item second[] = {{2, {90, 90}}};
    grid.Build(first, 1);
    grid.Build(second, 1);
    int count = 0;
    grid.ForEachCandidate({50, 50}, 100.0f, [&](std::uint32_t id, Vec2) {
        CHECK(id == 2);
        ++count;
    });
    CHECK(count == 1);
}
