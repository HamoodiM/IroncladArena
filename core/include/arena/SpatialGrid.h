#pragma once
// Uniform grid for broad-phase queries, rebuilt every tick with a counting sort.
//
// Build() is a counting sort: count items per cell, prefix-sum the counts into
// start offsets, then scatter the items into one flat array. Each
// cell's items end up contiguous, nothing is allocated after Configure(), and
// there are no per-cell vectors or linked lists to chase.
//
// Items are stored by centre point. A query for "anything that might touch a
// circle" therefore has to grow its radius by the largest item radius; the
// caller does the exact test on the candidates it gets back.
#include <cstdint>
#include <vector>

#include "arena/Math.h"

namespace arena {

class SpatialGrid {
public:
    struct Item {
        std::uint32_t id;
        Vec2 pos;
    };

    // Allocates. Call once at load, not during play.
    void Configure(Vec2 worldMin, Vec2 worldMax, float cellSize, std::uint32_t maxItems);

    // Replaces the contents with `items`. Items past MaxItems() are ignored and
    // counted in Overflow(). Positions outside the world clamp to edge cells.
    void Build(const Item* items, std::uint32_t count);

    // Calls f(id, pos) for every item whose cell overlaps the square around
    // `center` with half-size `radius`. Order is deterministic (cell order,
    // then insertion order).
    template <class F>
    void ForEachCandidate(Vec2 center, float radius, F&& f) const {
        int x0, y0, x1, y1;
        CellRange(center, radius, x0, y0, x1, y1);
        for (int cy = y0; cy <= y1; ++cy) {
            for (int cx = x0; cx <= x1; ++cx) {
                const std::uint32_t cell = static_cast<std::uint32_t>(cy * m_cols + cx);
                for (std::uint32_t i = m_cellStart[cell]; i < m_cellStart[cell + 1]; ++i) f(m_sorted[i].id, m_sorted[i].pos);
            }
        }
    }

    // Convenience: ids whose centre is within `radius` of `center`.
    template <class F>
    void ForEachInRadius(Vec2 center, float radius, F&& f) const {
        const float r2 = radius * radius;
        ForEachCandidate(center, radius, [&](std::uint32_t id, Vec2 p) {
            if (DistanceSq(p, center) <= r2) f(id, p);
        });
    }

    int Columns() const { return m_cols; }
    int Rows() const { return m_rows; }
    float CellSize() const { return m_cellSize; }
    Vec2 WorldMin() const { return m_min; }
    std::uint32_t CellCount(int cx, int cy) const;
    std::uint32_t MaxItems() const { return m_maxItems; }
    std::uint32_t Size() const { return m_count; }
    std::uint32_t Overflow() const { return m_overflow; }

private:
    int CellX(float x) const;
    int CellY(float y) const;
    void CellRange(Vec2 c, float r, int& x0, int& y0, int& x1, int& y1) const;

    Vec2 m_min{}, m_max{};
    float m_cellSize = 1.0f;
    float m_invCell = 1.0f;
    int m_cols = 0, m_rows = 0;
    std::uint32_t m_maxItems = 0;
    std::uint32_t m_count = 0;
    std::uint32_t m_overflow = 0;
    std::vector<std::uint32_t> m_cellStart;  // cols*rows + 1 offsets into m_sorted
    std::vector<std::uint32_t> m_itemCell;   // scratch: cell of each input item
    std::vector<std::uint32_t> m_cursor;     // scratch: next write slot per cell
    std::vector<Item> m_sorted;
};

}  // namespace arena
