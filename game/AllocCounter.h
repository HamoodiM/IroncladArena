#pragma once
// Counts every C++ heap allocation (global operator new) in the process. The
// debug overlay shows the count per frame: during a wave it should stay at 0,
// because the simulation only uses pools and pre-sized buffers.
//
// It doesn't see raylib's own C malloc calls, so the overlay labels the
// number "C++ allocations".
#include <cstdint>

namespace alloc {
std::uint64_t Count();
}
