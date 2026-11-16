#include "arena/SpatialGrid.h"

#include <algorithm>
#include <cmath>

namespace arena {

void SpatialGrid::Configure(Vec2 worldMin, Vec2 worldMax, float cellSize, std::uint32_t maxItems) {
    m_min = worldMin;
    m_max = worldMax;
    m_cellSize = cellSize;
    m_invCell = 1.0f / cellSize;
    m_cols = std::max(1, static_cast<int>(std::ceil((worldMax.x - worldMin.x) * m_invCell)));
    m_rows = std::max(1, static_cast<int>(std::ceil((worldMax.y - worldMin.y) * m_invCell)));
    m_maxItems = maxItems;
    m_cellStart.assign(static_cast<std::size_t>(m_cols) * m_rows + 1, 0);
    m_itemCell.assign(maxItems, 0);
    m_sorted.assign(maxItems, Item{0, {}});
    m_cursor.assign(static_cast<std::size_t>(m_cols) * m_rows, 0);
    m_count = 0;
    m_overflow = 0;
}

int SpatialGrid::CellX(float x) const {
    const int c = static_cast<int>((x - m_min.x) * m_invCell);
    return std::clamp(c, 0, m_cols - 1);
}

int SpatialGrid::CellY(float y) const {
    const int c = static_cast<int>((y - m_min.y) * m_invCell);
    return std::clamp(c, 0, m_rows - 1);
}

void SpatialGrid::CellRange(Vec2 c, float r, int& x0, int& y0, int& x1, int& y1) const {
    x0 = CellX(c.x - r);
    x1 = CellX(c.x + r);
    y0 = CellY(c.y - r);
    y1 = CellY(c.y + r);
}

void SpatialGrid::Build(const Item* items, std::uint32_t count) {
    m_overflow = count > m_maxItems ? count - m_maxItems : 0;
    m_count = std::min(count, m_maxItems);
    const std::size_t cells = m_cellStart.size() - 1;

    // Pass 1: count items per cell. Counts go one slot ahead so the prefix
    // sum below turns them directly into start offsets.
    std::fill(m_cellStart.begin(), m_cellStart.end(), 0u);
    for (std::uint32_t i = 0; i < m_count; ++i) {
        const auto cell = static_cast<std::uint32_t>(CellY(items[i].pos.y) * m_cols + CellX(items[i].pos.x));
        m_itemCell[i] = cell;
        ++m_cellStart[cell + 1];
    }
    for (std::size_t c = 1; c <= cells; ++c) m_cellStart[c] += m_cellStart[c - 1];

    // Pass 2: scatter each item to its cell's next free slot. Walking the input
    // in order keeps insertion order inside each cell, so queries are
    // deterministic.
    std::copy(m_cellStart.begin(), m_cellStart.end() - 1, m_cursor.begin());
    for (std::uint32_t i = 0; i < m_count; ++i) m_sorted[m_cursor[m_itemCell[i]]++] = items[i];
}

std::uint32_t SpatialGrid::CellCount(int cx, int cy) const {
    if (cx < 0 || cy < 0 || cx >= m_cols || cy >= m_rows) return 0;
    const std::size_t cell = static_cast<std::size_t>(cy) * m_cols + cx;
    return m_cellStart[cell + 1] - m_cellStart[cell];
}

}  // namespace arena
