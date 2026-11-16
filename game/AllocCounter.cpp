#include "AllocCounter.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<std::uint64_t> g_allocs{0};

void* Allocate(std::size_t size) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (size == 0) size = 1;
    if (void* p = std::malloc(size)) return p;
    throw std::bad_alloc();
}

void* AllocateAligned(std::size_t size, std::align_val_t align) {
    g_allocs.fetch_add(1, std::memory_order_relaxed);
    if (size == 0) size = 1;
#if defined(_MSC_VER)
    if (void* p = _aligned_malloc(size, static_cast<std::size_t>(align))) return p;
#else
    const std::size_t a = static_cast<std::size_t>(align);
    if (void* p = std::aligned_alloc(a, (size + a - 1) / a * a)) return p;
#endif
    throw std::bad_alloc();
}

void FreeAligned(void* p) noexcept {
#if defined(_MSC_VER)
    _aligned_free(p);
#else
    std::free(p);
#endif
}
}  // namespace

namespace alloc {
std::uint64_t Count() { return g_allocs.load(std::memory_order_relaxed); }
}  // namespace alloc

void* operator new(std::size_t size) { return Allocate(size); }
void* operator new[](std::size_t size) { return Allocate(size); }
void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
    try { return Allocate(size); } catch (...) { return nullptr; }
}
void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
    try { return Allocate(size); } catch (...) { return nullptr; }
}
void* operator new(std::size_t size, std::align_val_t a) { return AllocateAligned(size, a); }
void* operator new[](std::size_t size, std::align_val_t a) { return AllocateAligned(size, a); }

void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }
void operator delete(void* p, std::align_val_t) noexcept { FreeAligned(p); }
void operator delete[](void* p, std::align_val_t) noexcept { FreeAligned(p); }
void operator delete(void* p, std::size_t, std::align_val_t) noexcept { FreeAligned(p); }
void operator delete[](void* p, std::size_t, std::align_val_t) noexcept { FreeAligned(p); }
