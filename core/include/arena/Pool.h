#pragma once
// Fixed-capacity object pool with generational handles.
//
// All storage lives inside the pool, so spawning and despawning never touch the
// heap. A Handle remembers the generation of the slot it was issued for; once
// the slot is freed and reused, old handles stop resolving instead of silently
// pointing at a different object.
#include <array>
#include <cstddef>
#include <cstdint>

namespace arena {

struct Handle {
    static constexpr std::uint32_t kInvalid = 0xFFFFFFFFu;
    std::uint32_t index = kInvalid;
    std::uint32_t generation = 0;

    constexpr bool IsValid() const { return index != kInvalid; }
    constexpr bool operator==(const Handle&) const = default;
};

template <class T, std::size_t Capacity>
class Pool {
    static_assert(Capacity > 0 && Capacity < Handle::kInvalid);

public:
    Pool() { Clear(); }

    // Returns an invalid handle when the pool is full. Callers decide whether
    // that matters (a missing spark doesn't; a missing enemy is a config bug).
    Handle Spawn(const T& value) {
        if (m_freeCount == 0) return {};
        const std::uint32_t index = m_free[--m_freeCount];
        m_items[index] = value;
        m_alive[index] = true;
        ++m_aliveCount;
        return {index, m_generation[index]};
    }

    bool Despawn(Handle h) {
        if (!Owns(h)) return false;
        DespawnIndex(h.index);
        return true;
    }

    void DespawnIndex(std::uint32_t index) {
        if (index >= Capacity || !m_alive[index]) return;
        m_alive[index] = false;
        ++m_generation[index];
        m_free[m_freeCount++] = index;
        --m_aliveCount;
    }

    T* Get(Handle h) { return Owns(h) ? &m_items[h.index] : nullptr; }
    const T* Get(Handle h) const { return Owns(h) ? &m_items[h.index] : nullptr; }

    bool IsAlive(std::uint32_t index) const { return index < Capacity && m_alive[index]; }
    T& At(std::uint32_t index) { return m_items[index]; }
    const T& At(std::uint32_t index) const { return m_items[index]; }
    Handle HandleOf(std::uint32_t index) const { return {index, m_generation[index]}; }

    // Visits live objects in index order (deterministic). Despawning the
    // current object inside the callback is safe.
    template <class F>
    void ForEach(F&& f) {
        for (std::uint32_t i = 0; i < Capacity; ++i)
            if (m_alive[i]) f(i, m_items[i]);
    }
    template <class F>
    void ForEach(F&& f) const {
        for (std::uint32_t i = 0; i < Capacity; ++i)
            if (m_alive[i]) f(i, m_items[i]);
    }

    void Clear() {
        m_alive.fill(false);
        // Hand out low indices first: index 0 is the first spawned.
        for (std::uint32_t i = 0; i < Capacity; ++i) m_free[i] = static_cast<std::uint32_t>(Capacity - 1 - i);
        m_freeCount = static_cast<std::uint32_t>(Capacity);
        m_aliveCount = 0;
    }

    std::size_t Size() const { return m_aliveCount; }
    bool Full() const { return m_freeCount == 0; }
    static constexpr std::size_t MaxSize() { return Capacity; }

private:
    bool Owns(Handle h) const {
        return h.index < Capacity && m_alive[h.index] && m_generation[h.index] == h.generation;
    }

    std::array<T, Capacity> m_items{};
    std::array<std::uint32_t, Capacity> m_generation{};
    std::array<std::uint32_t, Capacity> m_free{};
    std::array<bool, Capacity> m_alive{};
    std::uint32_t m_freeCount = 0;
    std::uint32_t m_aliveCount = 0;
};

}  // namespace arena
