#pragma once
// Player controller: movement, dash, input buffering, the melee combo and
// shooting. It only decides velocity and actions; World moves the player,
// resolves collisions and applies the hits.
#include "arena/GameData.h"
#include "arena/Math.h"

namespace arena {

// One simulation tick of input. `aim` is a world-space point (the mouse).
// Pressed flags are edges: true on the first tick after the button went down.
struct PlayerInput {
    Vec2 move;  // length <= 1
    Vec2 aim;
    bool meleePressed = false;
    bool dashPressed = false;
    bool shootHeld = false;
};

// The "game feel" switches. Turning them off is how the before/after
// comparison clip is made, and it shows what each one is for.
struct FeelSettings {
    bool acceleration = true;  // off: velocity snaps to the stick
    bool inputBuffer = true;   // off: a press only counts on a tick where the action is possible
    bool hitstop = true;       // off: no freeze frames on hits
    bool screenShake = true;   // renderer only
};

struct PlayerState {
    Vec2 pos, prevPos, vel;
    Vec2 facing{1.0f, 0.0f};
    int hp = 5;
    bool alive = true;

    float dashTime = 0.0f;      // remaining dash
    float dashCooldown = 0.0f;
    float iframes = 0.0f;       // from dashing
    float hurtInvuln = 0.0f;    // from being hit
    Vec2 dashDir;

    float meleeBuffer = 0.0f;   // > 0: a melee press is waiting
    float dashBuffer = 0.0f;

    int comboStep = 0;          // which swing comes next (0..2)
    float swingTime = 0.0f;     // remaining lockout of the current swing
    float comboTimer = 0.0f;    // > 0: the next swing continues the combo
    int lastSwing = -1;         // for the renderer
    float lastSwingAge = 99.0f; // seconds since the last swing started
    Vec2 swingDir{1.0f, 0.0f};

    float shotCooldown = 0.0f;

    bool Invulnerable() const { return iframes > 0.0f || hurtInvuln > 0.0f; }
    bool Dashing() const { return dashTime > 0.0f; }
    bool Swinging() const { return swingTime > 0.0f; }
};

struct PlayerActions {
    bool dashed = false;
    int swing = -1;  // combo index of a swing that started this tick
    Vec2 swingDir;
    bool shot = false;
    Vec2 shotDir;
};

void ResetPlayer(PlayerState& p, const PlayerTuning& t, Vec2 pos);

// Records this tick's button presses into the input buffers without running
// any other logic. World calls this during hitstop, so a press made in a
// freeze frame isn't lost.
void BufferPlayerPresses(PlayerState& p, const PlayerTuning& t, const FeelSettings& feel, const PlayerInput& in);

// Advances timers, starts dashes, swings and shots, and sets p.vel.
void UpdatePlayerMotor(PlayerState& p, const PlayerTuning& t, const FeelSettings& feel, const PlayerInput& in, float dt,
                       PlayerActions& out);

}  // namespace arena
