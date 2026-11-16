#include "arena/PlayerMotor.h"

namespace arena {
namespace {

void Tick(float& timer, float dt) { timer = timer > dt ? timer - dt : 0.0f; }

// Without buffering a press still has to survive until the end of the tick it
// arrived on, so it gets a lifetime shorter than one tick.
float BufferWindow(const PlayerTuning& t, const FeelSettings& feel) { return feel.inputBuffer ? t.inputBuffer : 1e-4f; }

Vec2 AimDirection(const PlayerState& p, const PlayerInput& in) {
    const Vec2 d = Normalize(in.aim - p.pos);
    return LengthSq(d) > 0.0f ? d : p.facing;
}

}  // namespace

void ResetPlayer(PlayerState& p, const PlayerTuning& t, Vec2 pos) {
    p = PlayerState{};
    p.pos = p.prevPos = pos;
    p.hp = t.maxHp;
}

void BufferPlayerPresses(PlayerState& p, const PlayerTuning& t, const FeelSettings& feel, const PlayerInput& in) {
    const float window = BufferWindow(t, feel);
    if (in.meleePressed) p.meleeBuffer = window;
    if (in.dashPressed) p.dashBuffer = window;
}

void UpdatePlayerMotor(PlayerState& p, const PlayerTuning& t, const FeelSettings& feel, const PlayerInput& in, float dt,
                       PlayerActions& out) {
    out = PlayerActions{};
    if (!p.alive) {
        p.vel = MoveToward(p.vel, {}, t.friction * dt);
        return;
    }

    // 1. Timers.
    Tick(p.dashCooldown, dt);
    Tick(p.iframes, dt);
    Tick(p.hurtInvuln, dt);
    Tick(p.shotCooldown, dt);
    Tick(p.swingTime, dt);
    Tick(p.comboTimer, dt);
    Tick(p.dashTime, dt);
    p.lastSwingAge += dt;
    if (p.comboTimer <= 0.0f) p.comboStep = 0;

    BufferPlayerPresses(p, t, feel, in);
    const Vec2 aimDir = AimDirection(p, in);

    // 2. Dash: highest priority, and it cancels a swing's recovery.
    if (p.dashBuffer > 0.0f && p.dashCooldown <= 0.0f && !p.Dashing()) {
        const Vec2 moveDir = Normalize(in.move);
        p.dashDir = LengthSq(moveDir) > 0.0f ? moveDir : p.facing;
        p.dashTime = t.dashTime;
        p.iframes = t.dashIframes;
        p.dashCooldown = t.dashTime + t.dashCooldown;
        p.swingTime = 0.0f;
        p.dashBuffer = 0.0f;
        out.dashed = true;
    }

    // 3. Melee. A press during a swing waits in the buffer and fires the
    // moment the lockout ends, which is what makes combos feel responsive.
    if (p.meleeBuffer > 0.0f && !p.Swinging() && !p.Dashing()) {
        const int step = p.comboTimer > 0.0f ? p.comboStep : 0;
        p.facing = aimDir;
        p.swingDir = aimDir;
        p.swingTime = t.meleeSwingTime[step];
        p.comboTimer = t.meleeSwingTime[step] + t.comboWindow;
        p.comboStep = (step + 1) % 3;
        p.lastSwing = step;
        p.lastSwingAge = 0.0f;
        p.meleeBuffer = 0.0f;
        p.vel += aimDir * t.meleeLunge;
        out.swing = step;
        out.swingDir = aimDir;
    }

    // 4. Shooting (held, on a cooldown, not while swinging or dashing).
    if (in.shootHeld && p.shotCooldown <= 0.0f && !p.Swinging() && !p.Dashing()) {
        p.shotCooldown = t.shotCooldown;
        p.facing = aimDir;
        out.shot = true;
        out.shotDir = aimDir;
    }

    // 5. Velocity.
    if (p.Dashing()) {
        p.vel = p.dashDir * t.dashSpeed;
    } else {
        const float scale = p.Swinging() ? t.meleeMoveScale : 1.0f;
        const Vec2 target = ClampLength(in.move, 1.0f) * (t.maxSpeed * scale);
        if (feel.acceleration) {
            // Accelerate toward the stick; use the (stronger) friction when
            // letting go, so stops are crisp but starts still have weight.
            const float rate = LengthSq(in.move) > 0.0f ? t.accel : t.friction;
            p.vel = MoveToward(p.vel, target, rate * dt);
        } else {
            p.vel = target;
        }
        if (!p.Swinging()) p.facing = aimDir;
    }

    // 6. Buffers expire.
    Tick(p.meleeBuffer, dt);
    Tick(p.dashBuffer, dt);
}

}  // namespace arena
