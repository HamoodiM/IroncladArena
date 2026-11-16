#include <doctest/doctest.h>

#include "arena/PlayerMotor.h"

using namespace arena;

namespace {
constexpr float dt = 1.0f / 60.0f;

struct Rig {
    PlayerTuning t;
    FeelSettings feel;
    PlayerState p;
    PlayerActions out;
    Rig() { ResetPlayer(p, t, {0, 0}); }

    PlayerActions Step(PlayerInput in = {}) {
        if (LengthSq(in.aim) == 0.0f) in.aim = p.pos + Vec2{100, 0};
        UpdatePlayerMotor(p, t, feel, in, dt, out);
        p.pos += p.vel * dt;
        return out;
    }
    void Run(int ticks, PlayerInput in = {}) {
        for (int i = 0; i < ticks; ++i) Step(in);
    }
};
}  // namespace

TEST_CASE("Acceleration: speed ramps up instead of snapping, and reaches max speed") {
    Rig r;
    PlayerInput right;
    right.move = {1, 0};
    r.Step(right);
    CHECK(r.p.vel.x == doctest::Approx(r.t.accel * dt));
    CHECK(r.p.vel.x < r.t.maxSpeed);
    r.Run(30, right);
    CHECK(r.p.vel.x == doctest::Approx(r.t.maxSpeed));
}

TEST_CASE("With acceleration off, velocity snaps to the stick (the 'feel off' mode)") {
    Rig r;
    r.feel.acceleration = false;
    PlayerInput right;
    right.move = {1, 0};
    r.Step(right);
    CHECK(r.p.vel.x == doctest::Approx(r.t.maxSpeed));
}

TEST_CASE("Releasing the stick stops using friction, which is faster than accel") {
    Rig r;
    PlayerInput right;
    right.move = {1, 0};
    r.Run(30, right);
    const int ticksToStop = [&] {
        for (int i = 1; i < 60; ++i) {
            r.Step();
            if (r.p.vel.x == 0.0f) return i;
        }
        return 999;
    }();
    CHECK(ticksToStop <= static_cast<int>(r.t.maxSpeed / (r.t.friction * dt)) + 1);
}

TEST_CASE("Dash: fixed speed, i-frames, cooldown") {
    Rig r;
    PlayerInput in;
    in.move = {0, 1};
    in.dashPressed = true;
    CHECK(r.Step(in).dashed);
    CHECK(r.p.Dashing());
    CHECK(r.p.Invulnerable());
    CHECK(r.p.vel.y == doctest::Approx(r.t.dashSpeed));

    // Pressing again during the dash or cooldown does nothing.
    r.Run(3);
    CHECK_FALSE(r.Step(in).dashed);
    r.Run(static_cast<int>((r.t.dashTime + r.t.dashCooldown) / dt) + 2);
    CHECK(r.Step(in).dashed);
}

TEST_CASE("Dash with no stick input goes where the player faces") {
    Rig r;
    PlayerInput in;
    in.aim = {0, -100};  // face up
    r.Step(in);
    in.dashPressed = true;
    r.Step(in);
    CHECK(r.p.dashDir.y == doctest::Approx(-1.0f));
}

TEST_CASE("Input buffer: a melee press during a swing fires as soon as the swing ends") {
    Rig r;
    PlayerInput press;
    press.meleePressed = true;
    CHECK(r.Step(press).swing == 0);

    // Press again 2 ticks before the first swing's lockout ends.
    const int lockoutTicks = static_cast<int>(r.t.meleeSwingTime[0] / dt);
    r.Run(lockoutTicks - 3);
    CHECK(r.Step(press).swing == -1);  // still swinging: buffered
    int fired = -1;
    for (int i = 0; i < 5 && fired < 0; ++i) fired = r.Step().swing;
    CHECK(fired == 1);  // the buffered press became combo swing 2
}

TEST_CASE("Without the input buffer, that same early press is lost") {
    Rig r;
    r.feel.inputBuffer = false;
    PlayerInput press;
    press.meleePressed = true;
    r.Step(press);
    const int lockoutTicks = static_cast<int>(r.t.meleeSwingTime[0] / dt);
    r.Run(lockoutTicks - 3);
    r.Step(press);
    int fired = -1;
    for (int i = 0; i < 10 && fired < 0; ++i) fired = r.Step().swing;
    CHECK(fired == -1);
}

TEST_CASE("Combo: three swings in a row, then it starts over; a pause resets it") {
    Rig r;
    PlayerInput press;
    press.meleePressed = true;
    std::vector<int> swings;
    for (int i = 0; i < 200 && swings.size() < 4; ++i) {
        const int s = r.Step(press).swing;  // mashing
        if (s >= 0) swings.push_back(s);
    }
    CHECK(swings == std::vector<int>{0, 1, 2, 0});

    Rig r2;
    r2.Step(press);  // swing 0
    r2.Run(static_cast<int>((r2.t.meleeSwingTime[0] + r2.t.comboWindow) / dt) + 2);
    CHECK(r2.Step(press).swing == 0);  // window expired: back to the first swing
}

TEST_CASE("Dash cancels a swing's recovery") {
    Rig r;
    PlayerInput press;
    press.meleePressed = true;
    r.Step(press);
    CHECK(r.p.Swinging());
    PlayerInput dash;
    dash.dashPressed = true;
    CHECK(r.Step(dash).dashed);
    CHECK_FALSE(r.p.Swinging());
}

TEST_CASE("Shooting respects its cooldown and is blocked while swinging") {
    Rig r;
    PlayerInput hold;
    hold.shootHeld = true;
    int shots = 0;
    for (int i = 0; i < 60; ++i) shots += r.Step(hold).shot;
    CHECK(shots == static_cast<int>(1.0f / r.t.shotCooldown) + 1);

    Rig r2;
    PlayerInput both;
    both.meleePressed = true;
    both.shootHeld = true;
    CHECK(r2.Step(both).swing == 0);
    CHECK_FALSE(r2.out.shot);
}

TEST_CASE("A dead player ignores input and slides to a stop") {
    Rig r;
    r.p.alive = false;
    r.p.vel = {100, 0};
    PlayerInput in;
    in.dashPressed = true;
    in.meleePressed = true;
    in.move = {1, 0};
    const auto a = r.Step(in);
    CHECK_FALSE(a.dashed);
    CHECK(a.swing == -1);
    CHECK(r.p.vel.x < 100.0f);
}
