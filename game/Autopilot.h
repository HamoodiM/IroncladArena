#pragma once
// A simple bot that plays the game. Used for the title-screen attract mode,
// for recording demo footage, and for smoke-testing a build (--autoplay).
// It only reads the World and produces PlayerInput, like a human would.
#include "arena/World.h"

namespace game {

class Autopilot {
public:
    arena::PlayerInput Think(const arena::World& world);

private:
    int m_tick = 0;
    int m_lastSwingTick = -100;
    float m_orbitSign = 1.0f;
};

}  // namespace game
