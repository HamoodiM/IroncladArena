#pragma once
// Fixed-timestep accumulator ("Fix Your Timestep").
//
// The simulation always advances in steps of exactly `stepSeconds`, whatever
// the display rate. The renderer draws between the last two simulated states
// using Alpha(). Time is counted in integer nanoseconds so long sessions don't
// drift the way an accumulating float would.
#include <cstdint>

namespace arena {

class FixedTimestep {
public:
    explicit FixedTimestep(double stepSeconds, int maxStepsPerFrame = 8)
        : m_stepNs(static_cast<std::int64_t>(stepSeconds * 1e9 + 0.5)), m_maxSteps(maxStepsPerFrame) {}

    // Adds a frame's worth of real time and returns how many fixed steps to run.
    // After a long stall (debugger, window drag) at most `maxStepsPerFrame` run
    // and the remaining backlog is dropped, so the game slows down instead of
    // spiralling.
    int Advance(double frameSeconds) {
        if (frameSeconds < 0.0) frameSeconds = 0.0;
        m_accumNs += static_cast<std::int64_t>(frameSeconds * 1e9 + 0.5);
        std::int64_t steps = m_accumNs / m_stepNs;
        if (steps > m_maxSteps) {
            steps = m_maxSteps;
            m_accumNs = 0;
            m_droppedFrames++;
        } else {
            m_accumNs -= steps * m_stepNs;
        }
        return static_cast<int>(steps);
    }

    // How far between the previous and current simulated state to draw, in [0, 1).
    float Alpha() const { return static_cast<float>(static_cast<double>(m_accumNs) / static_cast<double>(m_stepNs)); }
    float StepSeconds() const { return static_cast<float>(static_cast<double>(m_stepNs) * 1e-9); }
    int DroppedFrames() const { return m_droppedFrames; }
    void Reset() { m_accumNs = 0; }

private:
    std::int64_t m_stepNs;
    std::int64_t m_accumNs = 0;
    int m_maxSteps;
    int m_droppedFrames = 0;
};

}  // namespace arena
