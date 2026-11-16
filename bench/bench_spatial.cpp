// Broad-phase benchmark: find every overlapping pair among N circles, the way
// the enemy separation step does, by brute force vs the uniform grid.
//
//   bench_spatial            -> markdown table on stdout
//
// Both methods must report the same number of pairs, or the run fails.
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <vector>

#include "arena/SpatialGrid.h"

using namespace arena;
using Clock = std::chrono::steady_clock;

namespace {

constexpr float kRadius = 14.0f;  // grunt radius
constexpr Vec2 kMin{0, 0}, kMax{1600, 960};

std::vector<SpatialGrid::Item> RandomItems(std::uint32_t n, std::uint64_t seed) {
    Rng rng(seed);
    std::vector<SpatialGrid::Item> items(n);
    for (std::uint32_t i = 0; i < n; ++i) items[i] = {i, {rng.Range(kMin.x, kMax.x), rng.Range(kMin.y, kMax.y)}};
    return items;
}

std::uint64_t BrutePairs(const std::vector<SpatialGrid::Item>& items) {
    const float r2 = (2 * kRadius) * (2 * kRadius);
    std::uint64_t pairs = 0;
    for (std::size_t i = 0; i < items.size(); ++i)
        for (std::size_t j = i + 1; j < items.size(); ++j) pairs += DistanceSq(items[i].pos, items[j].pos) < r2;
    return pairs;
}

std::uint64_t GridPairs(SpatialGrid& grid, const std::vector<SpatialGrid::Item>& items) {
    grid.Build(items.data(), static_cast<std::uint32_t>(items.size()));
    const float r2 = (2 * kRadius) * (2 * kRadius);
    std::uint64_t pairs = 0;
    for (const auto& it : items) {
        grid.ForEachCandidate(it.pos, 2 * kRadius, [&](std::uint32_t id, Vec2 p) {
            if (id > it.id && DistanceSq(p, it.pos) < r2) ++pairs;
        });
    }
    return pairs;
}

template <class F>
double MedianMs(int runs, F&& f) {
    std::vector<double> t;
    for (int i = 0; i < runs; ++i) {
        const auto a = Clock::now();
        f();
        t.push_back(std::chrono::duration<double, std::milli>(Clock::now() - a).count());
    }
    std::sort(t.begin(), t.end());
    return t[t.size() / 2];
}

}  // namespace

int main() {
    std::printf("| Circles | Overlapping pairs | Brute force (ms) | Grid build + query (ms) | Speed-up |\n");
    std::printf("|---:|---:|---:|---:|---:|\n");
    volatile std::uint64_t sink = 0;
    for (std::uint32_t n : {100u, 250u, 500u, 1000u, 2000u, 4000u, 8000u}) {
        const auto items = RandomItems(n, 42 + n);
        SpatialGrid grid;
        grid.Configure(kMin, kMax, 64.0f, n);

        const std::uint64_t brute = BrutePairs(items);
        const std::uint64_t viaGrid = GridPairs(grid, items);
        if (brute != viaGrid) {
            std::fprintf(stderr, "MISMATCH at n=%u: brute %llu, grid %llu\n", n, static_cast<unsigned long long>(brute),
                         static_cast<unsigned long long>(viaGrid));
            return 1;
        }
        const int runs = n <= 1000 ? 51 : 11;
        const double bruteMs = MedianMs(runs, [&] { sink = sink + BrutePairs(items); });
        const double gridMs = MedianMs(runs, [&] { sink = sink + GridPairs(grid, items); });
        std::printf("| %u | %llu | %.3f | %.3f | %.1fx |\n", n, static_cast<unsigned long long>(brute), bruteMs, gridMs,
                    bruteMs / gridMs);
    }
    return 0;
}
