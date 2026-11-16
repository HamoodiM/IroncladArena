#pragma once
// Loads data/game.cfg (the real tuning file) for tests that need a whole game.
#include <doctest/doctest.h>

#include <fstream>
#include <sstream>
#include <string>

#include "arena/GameData.h"

inline arena::GameData LoadRealGameData() {
    std::ifstream file(ARENA_DATA_FILE, std::ios::binary);
    REQUIRE_MESSAGE(file.good(), "could not open " ARENA_DATA_FILE);
    std::stringstream ss;
    ss << file.rdbuf();
    arena::GameData data;
    std::string error;
    const bool ok = arena::LoadGameData(ss.str(), data, error);
    INFO(error);
    REQUIRE(ok);
    return data;
}
