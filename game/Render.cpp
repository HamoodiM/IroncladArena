#include "Render.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace game {

using namespace arena;

namespace {

constexpr Color kBg{14, 15, 20, 255};
constexpr Color kFloor{27, 30, 38, 255};
constexpr Color kGridLine{36, 40, 50, 255};
constexpr Color kWall{70, 76, 92, 255};
constexpr Color kPillar{44, 49, 61, 255};
constexpr Color kPillarRim{88, 96, 116, 255};
constexpr Color kPlayer{127, 214, 255, 255};
constexpr Color kPlayerDark{40, 92, 128, 255};
constexpr Color kTelegraph{255, 92, 64, 255};
constexpr Color kText{232, 234, 240, 255};
constexpr Color kDim{140, 146, 160, 255};

Vector2 V(Vec2 v) { return {v.x, v.y}; }
Color Rgba(std::uint32_t c) { return GetColor(c); }
Color Mix(Color a, Color b, float t) {
    t = Clamp(t, 0.0f, 1.0f);
    return {static_cast<unsigned char>(a.r + (b.r - a.r) * t), static_cast<unsigned char>(a.g + (b.g - a.g) * t),
            static_cast<unsigned char>(a.b + (b.b - a.b) * t), static_cast<unsigned char>(a.a + (b.a - a.a) * t)};
}
Color Darker(Color c, float k) {
    return {static_cast<unsigned char>(c.r * k), static_cast<unsigned char>(c.g * k), static_cast<unsigned char>(c.b * k), c.a};
}
float Deg(float radians) { return radians * 180.0f / kPi; }

void Shadow(Vec2 p, float r) { DrawEllipse(static_cast<int>(p.x + 3), static_cast<int>(p.y + r * 0.55f), r * 1.05f, r * 0.45f, Fade(BLACK, 0.35f)); }

// Distance along a ray (unit `dir`) to the first pillar or wall, up to maxDist.
float RayToObstacle(const World& world, Vec2 origin, Vec2 dir, float maxDist) {
    float best = maxDist;
    for (const Pillar& p : world.Pillars()) {
        const Vec2 m = origin - p.pos;
        const float b = Dot(m, dir);
        const float c = LengthSq(m) - p.radius * p.radius;
        const float disc = b * b - c;
        if (disc < 0.0f) continue;
        const float t = -b - std::sqrt(disc);
        if (t > 0.0f && t < best) best = t;
    }
    const Vec2 mn = World::kArenaMin, mx = World::kArenaMax;
    if (dir.x > 0) best = std::min(best, (mx.x - origin.x) / dir.x);
    if (dir.x < 0) best = std::min(best, (mn.x - origin.x) / dir.x);
    if (dir.y > 0) best = std::min(best, (mx.y - origin.y) / dir.y);
    if (dir.y < 0) best = std::min(best, (mn.y - origin.y) / dir.y);
    return std::max(best, 0.0f);
}

void TextCentered(const char* text, int cx, int y, int size, Color c) {
    DrawText(text, cx - MeasureText(text, size) / 2, y, size, c);
}

}  // namespace

void Renderer::Reset() {
    m_fxCount = 0;
    m_trauma = 0.0f;
    m_bannerAge = 99.0f;
    m_hurtFlash = 0.0f;
    m_camSnapped = false;
    for (auto& g : m_ghosts) g.age = 99.0f;
}

void Renderer::AddFx(const Fx& fx) {
    if (m_fxCount < static_cast<int>(m_fx.size())) m_fx[static_cast<std::size_t>(m_fxCount++)] = fx;
}

void Renderer::AddTrauma(float amount) { m_trauma = std::min(1.0f, m_trauma + amount); }

void Renderer::OnEvents(const World& world) {
    for (const GameEvent& e : world.Events()) {
        switch (e.type) {
            case EventType::EnemyHit: AddTrauma(0.12f); break;
            case EventType::PlayerSwing:
                if (e.value >= 2.0f) AddTrauma(0.08f);
                break;
            case EventType::EnemyKilled: {
                AddTrauma(0.18f);
                Fx fx;
                fx.kind = Fx::Kind::Ring;
                fx.pos = V(e.pos);
                fx.life = 0.35f;
                fx.radius = 46.0f;
                fx.color = Rgba(world.Data().enemies[static_cast<std::size_t>(e.enemyDef)].color);
                AddFx(fx);
                break;
            }
            case EventType::Slam: {
                const float d = Distance(e.pos, world.Player().pos);
                AddTrauma(d < 500.0f ? 0.45f : 0.2f);
                Fx fx;
                fx.kind = Fx::Kind::Slam;
                fx.pos = V(e.pos);
                fx.life = 0.4f;
                fx.radius = e.value;
                fx.color = Color{255, 210, 140, 255};
                AddFx(fx);
                break;
            }
            case EventType::PlayerHurt:
                AddTrauma(0.5f);
                m_hurtFlash = 0.25f;
                break;
            case EventType::PlayerDied: AddTrauma(0.8f); break;
            case EventType::WaveStarted:
                std::snprintf(m_banner, sizeof(m_banner), "WAVE %d", static_cast<int>(e.value));
                m_bannerAge = 0.0f;
                break;
            case EventType::WaveCleared:
                std::snprintf(m_banner, sizeof(m_banner), "WAVE %d CLEARED", static_cast<int>(e.value));
                m_bannerAge = 0.0f;
                break;
            default: break;
        }
    }
}

void Renderer::Update(float dt, const World& world, float alpha) {
    m_time += dt;
    m_trauma = std::max(0.0f, m_trauma - 1.8f * dt);
    m_hurtFlash = std::max(0.0f, m_hurtFlash - dt);
    m_bannerAge += dt;

    // Shake offset: trauma squared, so small hits barely move the camera and
    // big ones really do.
    const float amount = shakeEnabled ? m_trauma * m_trauma * 16.0f : 0.0f;
    auto rnd = [&] {
        m_seed = m_seed * 1664525u + 1013904223u;
        return static_cast<float>(m_seed >> 8) / 8388608.0f - 1.0f;
    };
    m_shake = {rnd() * amount, rnd() * amount};

    for (int i = 0; i < m_fxCount;) {
        Fx& f = m_fx[static_cast<std::size_t>(i)];
        f.age += dt;
        if (f.age >= f.life) f = m_fx[static_cast<std::size_t>(--m_fxCount)];
        else ++i;
    }

    // Camera follows the player with a little lag and leads toward the aim.
    {
        const PlayerState& pl = world.Player();
        const Vec2 goal = Lerp(pl.prevPos, pl.pos, alpha) + pl.facing * 60.0f;
        if (!m_camSnapped) {
            m_camTarget = goal;
            m_camSnapped = true;
        }
        m_camTarget = Lerp(m_camTarget, goal, 1.0f - std::exp(-6.0f * dt));
    }

    // Afterimages while dashing.
    for (auto& g : m_ghosts) g.age += dt;
    const PlayerState& p = world.Player();
    m_ghostTimer -= dt;
    if (p.Dashing() && m_ghostTimer <= 0.0f) {
        m_ghosts[static_cast<std::size_t>(m_ghostHead)] = {V(Lerp(p.prevPos, p.pos, alpha)), 0.0f};
        m_ghostHead = (m_ghostHead + 1) % static_cast<int>(m_ghosts.size());
        m_ghostTimer = 0.016f;
    }
}

Camera2D Renderer::WorldCamera(int screenW, int screenH, bool withShake) const {
    // Zoom so about 1100 x 620 world units are visible, then keep the view
    // inside the arena (plus a margin) so the camera never shows the void.
    Camera2D cam{};
    cam.zoom = std::min(screenW / 1100.0f, screenH / 620.0f);
    const float halfW = screenW * 0.5f / cam.zoom, halfH = screenH * 0.5f / cam.zoom;
    constexpr float margin = 60.0f;
    Vec2 target = m_camTarget;
    const Vec2 mn = World::kArenaMin - Vec2{margin, margin}, mx = World::kArenaMax + Vec2{margin, margin};
    target.x = (mx.x - mn.x < 2 * halfW) ? (mn.x + mx.x) * 0.5f : Clamp(target.x, mn.x + halfW, mx.x - halfW);
    target.y = (mx.y - mn.y < 2 * halfH) ? (mn.y + mx.y) * 0.5f : Clamp(target.y, mn.y + halfH, mx.y - halfH);
    cam.target = V(target);
    cam.offset = {screenW * 0.5f, screenH * 0.5f};
    if (withShake) {
        cam.offset.x += m_shake.x * cam.zoom;
        cam.offset.y += m_shake.y * cam.zoom;
    }
    return cam;
}

void Renderer::DrawWorld(const World& world, float alpha, bool debug) const {
    const auto& data = world.Data();

    // Floor, grid, walls.
    const Vec2 mn = World::kArenaMin, mx = World::kArenaMax;
    DrawRectangleV({mn.x - 18, mn.y - 18}, {mx.x - mn.x + 36, mx.y - mn.y + 36}, kWall);
    DrawRectangleV(V(mn), V(mx - mn), kFloor);
    for (float x = mn.x + 64; x < mx.x; x += 64) DrawLineV({x, mn.y}, {x, mx.y}, kGridLine);
    for (float y = mn.y + 64; y < mx.y; y += 64) DrawLineV({mn.x, y}, {mx.x, y}, kGridLine);

    if (debug) {
        // Spatial grid occupancy.
        const SpatialGrid& g = world.Grid();
        for (int cy = 0; cy < g.Rows(); ++cy)
            for (int cx = 0; cx < g.Columns(); ++cx) {
                const auto n = g.CellCount(cx, cy);
                if (n == 0) continue;
                DrawRectangleV({g.WorldMin().x + cx * g.CellSize(), g.WorldMin().y + cy * g.CellSize()}, {g.CellSize(), g.CellSize()},
                               Fade(GREEN, std::min(0.08f * n, 0.35f)));
            }
    }

    for (const Pillar& p : world.Pillars()) {
        Shadow(p.pos, p.radius);
        DrawCircleV(V(p.pos), p.radius, kPillar);
        DrawCircleLinesV(V(p.pos), p.radius, kPillarRim);
        DrawCircleV(V(p.pos + Vec2{-p.radius * 0.25f, -p.radius * 0.25f}), p.radius * 0.45f, Fade(kPillarRim, 0.25f));
    }

    // Telegraphs go under the bodies.
    world.Enemies().ForEach([&](std::uint32_t, const Enemy& e) {
        if (e.state != EnemyState::Windup) return;
        const EnemyDef& def = data.enemies[e.def];
        const Vec2 pos = Lerp(e.prevPos, e.pos, alpha);
        const float t = Clamp(e.stateTime / std::max(def.windup, 0.01f), 0.0f, 1.0f);
        const float blink = 0.5f + 0.5f * std::sin(m_time * (20.0f + 30.0f * t));
        switch (def.kind) {
            case EnemyKind::Melee: {
                const float reach = def.radius + def.lungeSpeed * def.active + 10.0f;
                const Vec2 end = pos + e.attackDir * reach;
                DrawLineEx(V(pos), V(end), def.radius * 1.6f, Fade(kTelegraph, 0.10f + 0.15f * t));
                DrawLineEx(V(pos), V(pos + e.attackDir * (reach * t)), 4.0f, Fade(kTelegraph, 0.6f + 0.4f * blink * t));
                break;
            }
            case EnemyKind::Ranged: {
                const Vec2 end = pos + e.attackDir * RayToObstacle(world, pos, e.attackDir, 700.0f);
                DrawLineEx(V(pos), V(end), 1.5f + 2.0f * t, Fade(Rgba(def.color), 0.25f + 0.5f * t * blink));
                break;
            }
            case EnemyKind::Brute: {
                const Vec2 c = pos + e.attackDir * (def.radius * 0.6f);
                DrawCircleV(V(c), def.slamRadius, Fade(kTelegraph, 0.08f));
                DrawCircleV(V(c), def.slamRadius * t, Fade(kTelegraph, 0.18f + 0.1f * blink));
                DrawCircleLinesV(V(c), def.slamRadius, Fade(kTelegraph, 0.5f + 0.5f * blink));
                break;
            }
        }
    });

    // Effects under bodies: slam shockwaves and death rings.
    for (int i = 0; i < m_fxCount; ++i) {
        const Fx& f = m_fx[static_cast<std::size_t>(i)];
        const float k = f.age / f.life;
        if (f.kind == Fx::Kind::Slam) {
            DrawRing(f.pos, f.radius * k * 0.9f, f.radius * (0.15f + k), 0, 360, 48, Fade(f.color, 0.6f * (1.0f - k)));
        } else if (f.kind == Fx::Kind::Ring) {
            DrawRing(f.pos, f.radius * k, f.radius * k + 4.0f, 0, 360, 32, Fade(f.color, 1.0f - k));
        }
    }

    // Enemies.
    world.Enemies().ForEach([&](std::uint32_t, const Enemy& e) {
        const EnemyDef& def = data.enemies[e.def];
        const Vec2 pos = Lerp(e.prevPos, e.pos, alpha);
        Color body = Rgba(def.color);
        if (e.state == EnemyState::Spawning) {
            const float t = Clamp(e.stateTime / 0.6f, 0.0f, 1.0f);
            DrawCircleLinesV(V(pos), def.radius * (2.5f - 1.5f * t), Fade(body, t));
            DrawCircleV(V(pos), def.radius * t, Fade(body, 0.35f * t));
            return;
        }
        Shadow(pos, def.radius);
        if (e.state == EnemyState::Windup) body = Mix(body, kTelegraph, 0.35f);
        if (e.hitFlash > 0.0f) body = Mix(body, WHITE, 0.85f);
        float r = def.radius;
        if (e.state == EnemyState::Windup) r *= 1.0f + 0.08f * std::sin(m_time * 40.0f);  // shiver
        DrawCircleV(V(pos), r, body);
        DrawCircleLinesV(V(pos), r, Darker(Rgba(def.color), 0.5f));
        // "Eye" in the direction it's looking.
        const Vec2 look = e.state == EnemyState::Chase ? Normalize(world.Player().pos - pos) : e.attackDir;
        DrawCircleV(V(pos + look * (r * 0.5f)), r * 0.22f, Darker(body, 0.3f));
        if (def.kind == EnemyKind::Brute) DrawCircleLinesV(V(pos), r * 0.65f, Darker(body, 0.6f));

        if (e.state == EnemyState::Stagger) {
            for (int s = 0; s < 3; ++s) {
                const float a = m_time * 8.0f + s * 2.094f;
                DrawCircleV(V(pos + Vec2{std::cos(a) * r, -r - 6.0f + std::sin(a) * 4.0f}), 2.5f, YELLOW);
            }
        }
        const float maxHp = def.maxHp;  // wave scaling can push hp above this; clamp the bar
        if (e.hp < maxHp || def.kind == EnemyKind::Brute) {
            const float w = r * 2.0f;
            const float frac = Clamp(e.hp / maxHp, 0.0f, 1.0f);
            DrawRectangleV({pos.x - w / 2, pos.y - r - 10}, {w, 4}, Fade(BLACK, 0.6f));
            DrawRectangleV({pos.x - w / 2, pos.y - r - 10}, {w * frac, 4}, Mix(RED, GREEN, frac));
        }
        if (debug) {
            DrawCircleLinesV(V(pos), def.radius, GREEN);
            char label[32];
            std::snprintf(label, sizeof(label), "#%u %s", e.serial, ToString(e.state));
            DrawText(label, static_cast<int>(pos.x - 20), static_cast<int>(pos.y + r + 4), 12, GREEN);
        }
    });

    // Projectiles.
    world.Projectiles().ForEach([&](std::uint32_t, const Projectile& p) {
        const Vec2 pos = Lerp(p.prevPos, p.pos, alpha);
        if (p.fromPlayer) {
            const Vec2 tail = pos - Normalize(p.vel) * 22.0f;
            DrawLineEx(V(tail), V(pos), p.radius * 1.6f, Fade(kPlayer, 0.35f));
            DrawCircleV(V(pos), p.radius, WHITE);
        } else {
            DrawCircleV(V(pos), p.radius * 2.0f, Fade(Color{190, 120, 255, 255}, 0.25f));
            DrawCircleV(V(pos), p.radius, Color{215, 170, 255, 255});
        }
        if (debug) DrawCircleLinesV(V(pos), p.radius, GREEN);
    });

    // Player.
    const PlayerState& pl = world.Player();
    const float pr = data.player.radius;
    const Vec2 ppos = Lerp(pl.prevPos, pl.pos, alpha);
    for (const Ghost& g : m_ghosts)
        if (g.age < 0.2f) DrawCircleV(g.pos, pr, Fade(kPlayer, 0.35f * (1.0f - g.age / 0.2f)));

    if (pl.alive) {
        // Melee arc.
        if (pl.lastSwing >= 0 && pl.lastSwingAge < 0.14f) {
            const float k = pl.lastSwingAge / 0.14f;
            const float half = data.player.meleeArcDeg * 0.5f;
            const float a = Deg(Angle(pl.swingDir));
            const bool finisher = pl.lastSwing == 2;
            const Color c = finisher ? Color{255, 240, 200, 255} : kPlayer;
            DrawRing(V(ppos), pr + 4.0f, data.player.meleeRange + (finisher ? 10.0f : 0.0f), a - half, a + half, 24,
                     Fade(c, 0.55f * (1.0f - k)));
            DrawRing(V(ppos), data.player.meleeRange - 4.0f, data.player.meleeRange + 2.0f, a - half, a + half, 24, Fade(WHITE, 0.8f * (1.0f - k)));
        }
        Shadow(ppos, pr);
        const bool blink = pl.hurtInvuln > 0.0f && std::fmod(m_time, 0.12f) < 0.06f;
        if (!blink) {
            const Color body = pl.Dashing() ? Color{200, 240, 255, 255} : kPlayer;
            DrawCircleV(V(ppos), pr, body);
            DrawCircleLinesV(V(ppos), pr, kPlayerDark);
            const Vec2 f = pl.facing;
            const Vec2 tip = ppos + f * (pr + 9.0f);
            const Vec2 l = ppos + f * (pr - 2.0f) + Perp(f) * 7.0f;
            const Vec2 r2 = ppos + f * (pr - 2.0f) - Perp(f) * 7.0f;
            DrawTriangle(V(tip), V(l), V(r2), WHITE);  // raylib wants counter-clockwise; draw both windings
            DrawTriangle(V(tip), V(r2), V(l), WHITE);
        }
        if (debug) {
            DrawCircleLinesV(V(ppos), pr, GREEN);
            DrawRing(V(ppos), data.player.meleeRange - 1, data.player.meleeRange, Deg(Angle(pl.facing)) - data.player.meleeArcDeg / 2,
                     Deg(Angle(pl.facing)) + data.player.meleeArcDeg / 2, 24, Fade(GREEN, 0.6f));
        }
    }

    // Particles on top.
    world.Particles().ForEach([&](std::uint32_t, const Particle& p) {
        const float k = Clamp(p.life / p.maxLife, 0.0f, 1.0f);
        DrawCircleV(V(Lerp(p.prevPos, p.pos, alpha)), p.size * (0.4f + 0.6f * k), Fade(Rgba(p.color), k));
    });
}

void Renderer::DrawHud(const World& world, int sw, int sh, bool feelOn, int best) const {
    const PlayerState& p = world.Player();
    const PlayerTuning& t = world.Data().player;
    char buf[96];

    // Hurt vignette.
    if (m_hurtFlash > 0.0f) {
        const Color c = Fade(RED, m_hurtFlash * 1.2f);
        DrawRectangleGradientV(0, 0, sw, sh / 5, c, BLANK);
        DrawRectangleGradientV(0, sh - sh / 5, sw, sh / 5, BLANK, c);
    }

    // HP.
    for (int i = 0; i < t.maxHp; ++i) {
        const Rectangle r{20.0f + i * 30.0f, 16.0f, 22.0f, 22.0f};
        DrawRectangleRounded(r, 0.3f, 4, i < p.hp ? Color{240, 80, 90, 255} : Color{60, 60, 72, 255});
    }
    // Dash meter.
    const float dashFrac = 1.0f - Clamp(p.dashCooldown / (t.dashTime + t.dashCooldown), 0.0f, 1.0f);
    DrawRectangle(20, 46, 140, 6, Color{50, 54, 66, 255});
    DrawRectangle(20, 46, static_cast<int>(140 * dashFrac), 6, dashFrac >= 1.0f ? kPlayer : kDim);
    DrawText("DASH", 166, 42, 14, kDim);
    // Combo pips.
    if (p.comboTimer > 0.0f) {
        const int done = p.comboStep == 0 ? 3 : p.comboStep;  // swings landed in the current combo
        for (int i = 0; i < 3; ++i) DrawCircle(28 + i * 16, 68, 5, i < done ? WHITE : Color{70, 74, 86, 255});
    }

    std::snprintf(buf, sizeof(buf), "WAVE %d", std::max(1, world.Wave()));
    TextCentered(buf, sw / 2, 12, 28, kText);
    std::snprintf(buf, sizeof(buf), "%d left", world.EnemiesRemaining());
    if (world.Phase() != WavePhase::Intermission) TextCentered(buf, sw / 2, 42, 16, kDim);

    std::snprintf(buf, sizeof(buf), "%d", world.Score());
    DrawText(buf, sw - 20 - MeasureText(buf, 28), 12, 28, kText);
    std::snprintf(buf, sizeof(buf), "BEST %d", best);
    DrawText(buf, sw - 20 - MeasureText(buf, 14), 44, 14, kDim);

    std::snprintf(buf, sizeof(buf), "FEEL %s  [F2]", feelOn ? "ON" : "OFF");
    DrawText(buf, 20, sh - 26, 14, feelOn ? kDim : Color{255, 140, 90, 255});

    if (m_bannerAge < 1.6f) {
        const float a = m_bannerAge < 0.2f ? m_bannerAge / 0.2f : (m_bannerAge > 1.2f ? (1.6f - m_bannerAge) / 0.4f : 1.0f);
        const int size = 64;
        TextCentered(m_banner, sw / 2 + 3, sh / 3 + 3, size, Fade(BLACK, 0.5f * a));
        TextCentered(m_banner, sw / 2, sh / 3, size, Fade(kText, a));
    }
}

void Renderer::DrawDebugPanel(const World& world, const FrameStats& stats, int sw, int) const {
    const int w = 300, h = 236, x = sw - w - 16, y = 80;
    DrawRectangle(x, y, w, h, Fade(BLACK, 0.75f));
    DrawRectangleLines(x, y, w, h, Fade(GREEN, 0.6f));
    char buf[96];
    int line = y + 8;
    auto row = [&](const char* text) {
        DrawText(text, x + 10, line, 14, GREEN);
        line += 18;
    };
    std::snprintf(buf, sizeof(buf), "FPS %d   sim ticks this frame %d", GetFPS(), stats.ticksThisFrame);
    row(buf);
    std::snprintf(buf, sizeof(buf), "enemies %zu/%zu  shots %zu  fx %zu", world.Enemies().Size(), World::kMaxEnemies,
                  world.Projectiles().Size(), world.Particles().Size());
    row(buf);
    std::snprintf(buf, sizeof(buf), "C++ allocations this frame: %llu", static_cast<unsigned long long>(stats.allocsThisFrame));
    row(buf);
    std::snprintf(buf, sizeof(buf), "  max in a frame this wave: %llu", static_cast<unsigned long long>(stats.allocsMaxDuringWave));
    row(buf);
    std::snprintf(buf, sizeof(buf), "tick %llu  hitstop %.2f  state %016llx", static_cast<unsigned long long>(world.Tick()),
                  world.Hitstop(), static_cast<unsigned long long>(world.StateHash() & 0xFFFFFFFFull));
    row(buf);

    // Frame time graph: 0-33 ms, with a 16.7 ms line.
    const int gx = x + 10, gy = line + 4, gw = w - 20, gh = 80;
    DrawRectangle(gx, gy, gw, gh, Fade(DARKGREEN, 0.25f));
    const float scale = gh / 33.3f;
    DrawLine(gx, gy + gh - static_cast<int>(16.7f * scale), gx + gw, gy + gh - static_cast<int>(16.7f * scale), Fade(YELLOW, 0.5f));
    const int n = static_cast<int>(stats.frameMs.size());
    for (int i = 0; i < n; ++i) {
        const float ms = stats.frameMs[static_cast<std::size_t>((stats.head + i) % n)];
        const int bh = std::min(gh, static_cast<int>(ms * scale));
        const int bx = gx + i * gw / n;
        DrawLine(bx, gy + gh, bx, gy + gh - bh, ms > 17.5f ? ORANGE : GREEN);
    }
    DrawText("frame ms (line = 16.7)", gx, gy + gh + 4, 12, Fade(GREEN, 0.7f));
}

}  // namespace game
