cmake_minimum_required(VERSION 3.24)
project(IroncladArena VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(ARENA_BUILD_GAME  "Build the raylib game executable" ON)
option(ARENA_BUILD_TESTS "Build the unit tests"             ON)
option(ARENA_BUILD_BENCH "Build the spatial grid benchmark" ON)

include(FetchContent)
# doctest 2.4.11 declares cmake_minimum_required(3.0), which CMake 4 rejects.
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

# ---------------------------------------------------------------------------
# arena_core: every gameplay rule. No raylib, no I/O beyond parsing text,
# so it builds and tests anywhere (CI runs it headless).
# ---------------------------------------------------------------------------
add_library(arena_core STATIC
    core/src/Config.cpp
    core/src/EnemyBrain.cpp
    core/src/GameData.cpp
    core/src/PlayerMotor.cpp
    core/src/SpatialGrid.cpp
    core/src/World.cpp
)
target_include_directories(arena_core PUBLIC core/include)

if(MSVC)
    target_compile_options(arena_core PRIVATE /W4 /permissive-)
else()
    target_compile_options(arena_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ---------------------------------------------------------------------------
# Game (raylib)
# ---------------------------------------------------------------------------
if(ARENA_BUILD_GAME)
    set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(BUILD_GAMES    OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(raylib
        GIT_REPOSITORY https://github.com/raysan5/raylib.git
        GIT_TAG        5.5
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(raylib)

    add_executable(ironclad_arena
        game/main.cpp
        game/AllocCounter.cpp
        game/Audio.cpp
        game/Autopilot.cpp
        game/Render.cpp
    )
    target_link_libraries(ironclad_arena PRIVATE arena_core raylib)
    if(MSVC)
        target_compile_options(ironclad_arena PRIVATE /W4 /permissive-)
        # Release builds are a window app with no console behind them.
        set_target_properties(ironclad_arena PROPERTIES
            LINK_FLAGS_RELEASE "/SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup")
    endif()
    add_custom_command(TARGET ironclad_arena POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_directory
                ${CMAKE_SOURCE_DIR}/data $<TARGET_FILE_DIR:ironclad_arena>/data)
endif()

# ---------------------------------------------------------------------------
# Tests (doctest)
# ---------------------------------------------------------------------------
if(ARENA_BUILD_TESTS)
    FetchContent_Declare(doctest
        GIT_REPOSITORY https://github.com/doctest/doctest.git
        GIT_TAG        v2.4.11
        GIT_SHALLOW    TRUE)
    FetchContent_MakeAvailable(doctest)

    add_executable(arena_tests
        tests/test_main.cpp
        tests/test_alloc.cpp
        tests/test_config.cpp
        tests/test_determinism.cpp
        tests/test_enemy_brain.cpp
        tests/test_math_pool.cpp
        tests/test_player_motor.cpp
        tests/test_spatial_grid.cpp
        tests/test_timestep.cpp
        tests/test_world.cpp
    )
    target_link_libraries(arena_tests PRIVATE arena_core doctest::doctest)
    target_compile_definitions(arena_tests PRIVATE
        ARENA_DATA_FILE="${CMAKE_SOURCE_DIR}/data/game.cfg")
    enable_testing()
    add_test(NAME arena_tests COMMAND arena_tests)
endif()

# ---------------------------------------------------------------------------
# Benchmark
# ---------------------------------------------------------------------------
if(ARENA_BUILD_BENCH)
    add_executable(bench_spatial bench/bench_spatial.cpp)
    target_link_libraries(bench_spatial PRIVATE arena_core)
endif()
