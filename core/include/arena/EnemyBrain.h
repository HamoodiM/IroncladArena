#pragma once
// Enemy behaviour as a small finite-state machine.
//
//   Spawning -> Chase -> Windup -> Attack -> Recover -> Chase
//                  \________ Stagger (any hit that breaks poise) ________/
//
// Every attack is telegraphed (Windup) and followed by a punish window
// (Recover). Melee and brute enemies lock their attack direction when the
// windup starts, so a dash at the right moment always works. The brain only
// decides; World moves the enemy and applies the attack.
#include <cstdint>

#include "arena/GameData.h"
#include "arena/Math.h"

namespace arena {

enum class EnemyState : std::uint8_t { Spawning, Idle, Chase, Windup, Attack, Recover, Stagger };

const char* ToString(EnemyState s);

struct Enemy {
    std::uint16_t def = 0;
    EnemyState state = EnemyState::Spawning;
    float stateTime = 0.0f;
    Vec2 pos, prevPos, vel;
    Vec2 attackDir{1.0f, 0.0f};
    float hp = 1.0f;
    float poise = 1.0f;
    float hitFlash = 0.0f;
    bool attackLanded = false;  // one hit per attack
    float strafeSign = 1.0f;    // ranged enemies circle one way or the other
    std::uint32_t serial = 0;   // spawn order, for stable debugging labels
};

struct BrainContext {
    Vec2 playerPos;
    float playerRadius = 16.0f;
    bool playerAlive = true;
    float spawnTime = 0.6f;
};

struct BrainOutput {
    Vec2 desiredVel;
    bool snapVelocity = false;  // lunges ignore acceleration
    bool startedWindup = false;
    bool beginAttack = false;   // the attack's first tick: World spawns the shot / slam
};

void UpdateEnemyBrain(Enemy& e, const EnemyDef& def, const BrainContext& ctx, float dt, BrainOutput& out);

// Interrupts whatever the enemy was doing.
void StaggerEnemy(Enemy& e, const EnemyDef& def);

bool IsTelegraphing(const Enemy& e);

}  // namespace arena
