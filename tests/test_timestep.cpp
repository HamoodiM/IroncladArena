#include <doctest/doctest.h>

#include <initializer_list>

#include "arena/FixedTimestep.h"

using namespace arena;

namespace {
int StepsOver(double seconds, double frameDt) {
    FixedTimestep ts(1.0 / 60.0);
    int steps = 0;
    const int frames = static_cast<int>(seconds / frameDt + 0.5);
    for (int i = 0; i < frames; ++i) steps += ts.Advance(frameDt);
    return steps;
}
}  // namespace

TEST_CASE("The simulation runs ~60 ticks per second at any display rate") {
    // Within one tick: the remainder sits in the accumulator.
    for (double fps : {30.0, 60.0, 75.0, 144.0, 240.0}) {
        const int steps = StepsOver(2.0, 1.0 / fps);
        INFO("fps = " << fps);
        CHECK(steps >= 119);
        CHECK(steps <= 120);
    }
}

TEST_CASE("Alpha stays in [0, 1)") {
    FixedTimestep ts(1.0 / 60.0);
    for (int i = 0; i < 500; ++i) {
        ts.Advance(1.0 / 144.0);
        CHECK(ts.Alpha() >= 0.0f);
        CHECK(ts.Alpha() < 1.0f);
    }
}

TEST_CASE("A long stall runs at most maxSteps and drops the backlog") {
    FixedTimestep ts(1.0 / 60.0, 8);
    CHECK(ts.Advance(5.0) == 8);
    CHECK(ts.DroppedFrames() == 1);
    CHECK(ts.Alpha() == 0.0f);
    CHECK(ts.Advance(1.0 / 60.0) == 1);
}

TEST_CASE("Negative frame time is ignored") {
    FixedTimestep ts(1.0 / 60.0);
    CHECK(ts.Advance(-1.0) == 0);
}
