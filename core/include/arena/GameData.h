#pragma once
// Tuning data loaded from data/game.cfg: the player, every enemy type and the
// wave list. Changing a number there changes the game without a rebuild.
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace arena {

struct PlayerTuning {
    float radius = 16.0f;
    int maxHp = 5;
    float maxSpeed = 330.0f;
    float accel = 2600.0f;      // units/s^2 toward the stick direction
    float friction = 3200.0f;   // units/s^2 back to rest with no input
    float dashSpeed = 900.0f;
    float dashTime = 0.16f;
    float dashCooldown = 0.45f;
    float dashIframes = 0.20f;  // a little longer than the dash itself
    float inputBuffer = 0.12f;  // a press this early still counts
    float hurtInvuln = 0.8f;

    // Melee combo: three swings, the third is a heavy finisher.
    float meleeRange = 62.0f;
    float meleeArcDeg = 120.0f;
    float meleeDamage[3] = {2.0f, 2.0f, 4.0f};
    float meleeKnockback[3] = {260.0f, 260.0f, 620.0f};
    float meleeSwingTime[3] = {0.20f, 0.20f, 0.34f};  // lockout before the next swing
    float comboWindow = 0.30f;                         // after a swing ends
    float meleeMoveScale = 0.35f;                      // movement while swinging
    float meleeLunge = 160.0f;                         // small step into each swing

    float shotSpeed = 950.0f;
    float shotDamage = 1.0f;
    float shotCooldown = 0.22f;
    float shotRadius = 5.0f;
    float shotKnockback = 90.0f;
};

enum class EnemyKind : std::uint8_t { Melee, Ranged, Brute };

struct EnemyDef {
    std::string name;
    EnemyKind kind = EnemyKind::Melee;
    float radius = 14.0f;
    float maxHp = 4.0f;
    float speed = 150.0f;
    float accel = 900.0f;
    float attackRange = 40.0f;     // edge-to-edge distance that starts a windup
    float windup = 0.45f;          // telegraph time
    float active = 0.18f;          // lunge / slam / shot time
    float recover = 0.55f;         // punish window
    float damage = 1.0f;
    float lungeSpeed = 520.0f;     // Melee
    float slamRadius = 90.0f;      // Brute
    float projectileSpeed = 420.0f;  // Ranged
    float preferredRange = 300.0f;   // Ranged keeps this distance
    float poise = 1.0f;            // damage it takes to stagger
    float staggerTime = 0.35f;
    float knockbackScale = 1.0f;   // heavy enemies get pushed less
    int score = 100;
    std::uint32_t color = 0xE05A47FFu;  // RGBA, used by the renderer only
};

struct WaveDef {
    struct Group {
        int enemy;  // index into GameData::enemies
        int count;
    };
    std::vector<Group> groups;
    float spawnInterval = 0.6f;
};

struct GameData {
    PlayerTuning player;
    std::vector<EnemyDef> enemies;
    std::vector<WaveDef> waves;
    float intermission = 2.5f;      // seconds between waves
    float extraWaveHpScale = 1.15f; // after the last wave: repeat it, harder each time
    int extraWaveCount = 2;         // extra enemies per group each repeat

    int FindEnemy(std::string_view name) const;
};

// Returns false and fills `error` on a syntax error, an unknown key, a bad
// number, or a wave that names an enemy that doesn't exist.
bool LoadGameData(std::string_view text, GameData& out, std::string& error);

}  // namespace arena
