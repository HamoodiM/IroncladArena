// Ironclad Arena: entry point, input, screens and the frame loop.
//
//   ironclad_arena                    play
//   --autoplay                        the bot plays (also skips the title)
//   --seed N                          fixed seed (default: from the clock)
//   --feel-off                        start with the game-feel features off
//   --debug                           start with the debug overlay on
//   --size 1280x720                   window size
//   --frames N                        quit after N frames
//   --record DIR                      save every frame as DIR/frame_00001.png,
//                                     stepping exactly 1/60 s per frame
//   --shot N PATH                     save frame N to PATH (repeatable)
//   --mute                            no audio
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include <raylib.h>
#include <rlgl.h>

#include "AllocCounter.h"
#include "Audio.h"
#include "Autopilot.h"
#include "Render.h"
#include "arena/FixedTimestep.h"
#include "arena/GameData.h"
#include "arena/World.h"

using namespace arena;

namespace {

enum class Screen { Title, Playing, Paused, GameOver };

struct Options {
    bool autoplay = false;
    bool seedGiven = false;
    std::uint64_t seed = 1;
    bool feelOff = false;
    bool debug = false;
    bool mute = false;
    int width = 1600, height = 900;
    int frames = 0;
    std::string recordDir;
    std::vector<std::pair<int, std::string>> shots;
};

Options ParseArgs(int argc, char** argv) {
    Options o;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : std::string(); };
        if (a == "--autoplay") o.autoplay = true;
        else if (a == "--seed") { o.seed = std::strtoull(next().c_str(), nullptr, 10); o.seedGiven = true; }
        else if (a == "--feel-off") o.feelOff = true;
        else if (a == "--debug") o.debug = true;
        else if (a == "--mute") o.mute = true;
        else if (a == "--frames") o.frames = std::atoi(next().c_str());
        else if (a == "--record") o.recordDir = next();
        else if (a == "--size") {
            const std::string s = next();
            char* end = nullptr;
            o.width = static_cast<int>(std::strtol(s.c_str(), &end, 10));
            if (end && *end == 'x') o.height = static_cast<int>(std::strtol(end + 1, nullptr, 10));
        } else if (a == "--shot") {
            const int frame = std::atoi(next().c_str());
            o.shots.emplace_back(frame, next());
        }
    }
    return o;
}

bool LoadData(GameData& out, std::string& error) {
    const std::string candidates[] = {std::string(GetApplicationDirectory()) + "data/game.cfg", "data/game.cfg"};
    for (const auto& path : candidates) {
        std::ifstream f(path, std::ios::binary);
        if (!f) continue;
        std::stringstream ss;
        ss << f.rdbuf();
        return LoadGameData(ss.str(), out, error);
    }
    error = "data/game.cfg not found next to the executable";
    return false;
}

void SetFeel(World& w, bool on) {
    w.feel.acceleration = w.feel.inputBuffer = w.feel.hitstop = w.feel.screenShake = on;
}

// Button presses can happen on frames where no simulation tick runs (high
// refresh rates), so they're collected here until the next tick consumes them.
struct PendingInput {
    bool melee = false, dash = false;
    void Clear() { melee = dash = false; }
};

PlayerInput ReadHumanInput(const World& w, const Camera2D& cam, PendingInput& pending, bool& usingStickAim) {
    PlayerInput in;
    Vec2 move{static_cast<float>((IsKeyDown(KEY_D) || IsKeyDown(KEY_RIGHT)) - (IsKeyDown(KEY_A) || IsKeyDown(KEY_LEFT))),
              static_cast<float>((IsKeyDown(KEY_S) || IsKeyDown(KEY_DOWN)) - (IsKeyDown(KEY_W) || IsKeyDown(KEY_UP)))};
    const Vector2 mouse = GetScreenToWorld2D(GetMousePosition(), cam);
    in.aim = {mouse.x, mouse.y};
    if (LengthSq(Vec2{GetMouseDelta().x, GetMouseDelta().y}) > 1.0f) usingStickAim = false;

    if (IsGamepadAvailable(0)) {
        const Vec2 stick{GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_X), GetGamepadAxisMovement(0, GAMEPAD_AXIS_LEFT_Y)};
        if (Length(stick) > 0.2f) move = stick;
        const Vec2 look{GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_X), GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_Y)};
        if (Length(look) > 0.35f) usingStickAim = true;
        if (usingStickAim) in.aim = w.Player().pos + Normalize(Length(look) > 0.35f ? look : w.Player().facing) * 200.0f;
        in.shootHeld |= GetGamepadAxisMovement(0, GAMEPAD_AXIS_RIGHT_TRIGGER) > 0.3f || IsGamepadButtonDown(0, GAMEPAD_BUTTON_RIGHT_TRIGGER_2);
    }
    in.move = ClampLength(move, 1.0f);
    in.shootHeld |= IsMouseButtonDown(MOUSE_BUTTON_RIGHT) || IsKeyDown(KEY_K);
    in.meleePressed = pending.melee;
    in.dashPressed = pending.dash;
    return in;
}

void CollectPresses(PendingInput& p) {
    p.melee |= IsMouseButtonPressed(MOUSE_BUTTON_LEFT) || IsKeyPressed(KEY_J) ||
               (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_LEFT));
    p.dash |= IsKeyPressed(KEY_SPACE) || IsKeyPressed(KEY_LEFT_SHIFT) ||
              (IsGamepadAvailable(0) && (IsGamepadButtonPressed(0, GAMEPAD_BUTTON_RIGHT_FACE_DOWN) ||
                                         IsGamepadButtonPressed(0, GAMEPAD_BUTTON_LEFT_TRIGGER_1)));
}

void DrawCentered(const char* text, int cx, int y, int size, Color c) { DrawText(text, cx - MeasureText(text, size) / 2, y, size, c); }

void DrawControls(int cx, int y) {
    const char* lines[] = {
        "WASD / left stick      move",
        "Mouse / right stick    aim",
        "Left click / J / X     melee (3-hit combo)",
        "Right click / K / RT   shoot",
        "Space / Shift / A      dash (invulnerable)",
        "F1 debug   F2 feel on/off   F3 mute   F5 reload data   Esc pause",
    };
    for (const char* l : lines) {
        DrawText(l, cx - 250, y, 18, Color{190, 196, 210, 255});
        y += 26;
    }
}

}  // namespace

int main(int argc, char** argv) {
    Options opt = ParseArgs(argc, argv);
    if (!opt.seedGiven)
        opt.seed = static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count());
    const bool recording = !opt.recordDir.empty() || !opt.shots.empty();

    SetConfigFlags(FLAG_MSAA_4X_HINT | FLAG_VSYNC_HINT | FLAG_WINDOW_RESIZABLE);
    SetTraceLogLevel(LOG_WARNING);
    InitWindow(opt.width, opt.height, "Ironclad Arena");
    SetExitKey(KEY_NULL);  // Esc pauses instead of quitting

    game::Audio audio;
    if (!opt.mute) audio.Init();

    GameData data;
    std::string error;
    if (!LoadData(data, error)) {
        while (!WindowShouldClose()) {
            BeginDrawing();
            ClearBackground(Color{14, 15, 20, 255});
            DrawText("Could not load game data:", 40, 40, 24, RED);
            DrawText(error.c_str(), 40, 80, 20, RAYWHITE);
            EndDrawing();
        }
        CloseWindow();
        return 1;
    }

    auto world = std::make_unique<World>(data, opt.seed);
    auto attract = std::make_unique<World>(data, opt.seed ^ 0xA77AC7ull);  // title-screen demo
    SetFeel(*world, !opt.feelOff);
    game::Renderer renderer;
    renderer.shakeEnabled = !opt.feelOff;
    game::Autopilot bot, attractBot;
    FixedTimestep timestep(World::kStep);
    game::FrameStats stats;
    PendingInput pending;
    bool usingStickAim = false;
    bool debug = opt.debug;
    bool feelOn = !opt.feelOff;
    int best = 0;
    float gameOverDelay = 0.0f;
    std::string toast;
    float toastTime = 0.0f;
    Screen screen = opt.autoplay ? Screen::Playing : Screen::Title;  // capture without --autoplay shows the title
    const bool botPlays = opt.autoplay;
    int frame = 0;

    auto restart = [&] {
        opt.seed = opt.seed * 6364136223846793005ull + 1442695040888963407ull;
        world = std::make_unique<World>(data, opt.seed);
        SetFeel(*world, feelOn);
        renderer.Reset();
        timestep.Reset();
        pending.Clear();
        bot = game::Autopilot{};
        gameOverDelay = 0.0f;
        screen = Screen::Playing;
    };

    while (!WindowShouldClose()) {
        ++frame;
        const std::uint64_t allocsBefore = alloc::Count();
        const float frameDt = recording ? World::kStep : std::min(GetFrameTime(), 0.25f);
        const int sw = GetScreenWidth(), sh = GetScreenHeight();

        // ---- Keys that work everywhere.
        if (IsKeyPressed(KEY_F1)) debug = !debug;
        if (IsKeyPressed(KEY_F2)) {
            feelOn = !feelOn;
            SetFeel(*world, feelOn);
            renderer.shakeEnabled = feelOn;
            toast = feelOn ? "Feel ON: acceleration, input buffer, hitstop, screen shake"
                           : "Feel OFF: instant velocity, no buffer, no hitstop, no shake";
            toastTime = 3.0f;
        }
        if (IsKeyPressed(KEY_F3)) audio.SetMuted(!audio.Muted());
        if (IsKeyPressed(KEY_F5)) {
            GameData fresh;
            std::string err;
            if (LoadData(fresh, err)) {
                data = std::move(fresh);
                restart();
                attract = std::make_unique<World>(data, opt.seed ^ 0xA77AC7ull);
                toast = "Reloaded data/game.cfg";
            } else {
                toast = "Reload failed: " + err;
            }
            toastTime = 4.0f;
        }

        // ---- Screen logic.
        const Screen screenBefore = screen;
        switch (screen) {
            case Screen::Title:
                if (IsKeyPressed(KEY_ENTER) || IsMouseButtonPressed(MOUSE_BUTTON_LEFT) ||
                    (IsGamepadAvailable(0) && IsGamepadButtonPressed(0, GAMEPAD_BUTTON_MIDDLE_RIGHT)))
                    restart();
                break;
            case Screen::Playing:
                if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) screen = Screen::Paused;
                break;
            case Screen::Paused:
                if (IsKeyPressed(KEY_ESCAPE) || IsKeyPressed(KEY_P)) { screen = Screen::Playing; timestep.Reset(); }
                if (IsKeyPressed(KEY_Q)) screen = Screen::Title;
                break;
            case Screen::GameOver:
                if (IsKeyPressed(KEY_R) || IsKeyPressed(KEY_ENTER)) restart();
                if (IsKeyPressed(KEY_ESCAPE)) screen = Screen::Title;
                break;
        }

        const bool startedThisFrame = screenBefore != Screen::Playing && screen == Screen::Playing;

        // ---- Simulation.
        const Camera2D aimCam = renderer.WorldCamera(sw, sh, false);
        stats.ticksThisFrame = 0;
        if (screen == Screen::Playing || screen == Screen::GameOver) {
            if (!startedThisFrame) CollectPresses(pending);  // the click that started the run isn't an attack
            const int steps = timestep.Advance(frameDt);
            for (int s = 0; s < steps; ++s) {
                const PlayerInput in = botPlays ? bot.Think(*world) : ReadHumanInput(*world, aimCam, pending, usingStickAim);
                pending.Clear();
                world->Step(in);
                renderer.OnEvents(*world);
                audio.OnEvents(*world);
                for (const auto& e : world->Events())
                    if (e.type == EventType::WaveStarted) stats.allocsMaxDuringWave = 0;
                ++stats.ticksThisFrame;
            }
            best = std::max(best, world->Score());
            if (world->GameOver() && screen == Screen::Playing) {
                gameOverDelay += frameDt;
                if (gameOverDelay > 1.2f) screen = Screen::GameOver;
            }
        } else if (screen == Screen::Title) {
            const int steps = timestep.Advance(frameDt);
            for (int s = 0; s < steps; ++s) {
                attract->Step(attractBot.Think(*attract));
                if (attract->GameOver() && attract->Tick() % 240 == 0) {
                    attract = std::make_unique<World>(data, attract->Tick());
                    attractBot = game::Autopilot{};
                }
            }
        }
        const World& shown = screen == Screen::Title ? *attract : *world;
        const float alpha = (screen == Screen::Paused) ? 1.0f : timestep.Alpha();
        if (screen != Screen::Paused) renderer.Update(frameDt, shown, alpha);

        // ---- Draw.
        BeginDrawing();
        ClearBackground(Color{14, 15, 20, 255});
        BeginMode2D(renderer.WorldCamera(sw, sh, true));
        renderer.DrawWorld(shown, alpha, debug && screen != Screen::Title);
        EndMode2D();

        char buf[128];
        switch (screen) {
            case Screen::Title:
                DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.62f));
                DrawCentered("IRONCLAD ARENA", sw / 2 + 4, sh / 5 + 4, 84, Fade(BLACK, 0.6f));
                DrawCentered("IRONCLAD ARENA", sw / 2, sh / 5, 84, Color{127, 214, 255, 255});
                DrawCentered("Survive the waves. Read the telegraphs. Dash through.", sw / 2, sh / 5 + 100, 22, RAYWHITE);
                DrawControls(sw / 2, sh / 5 + 160);
                if (std::fmod(GetTime(), 1.0) < 0.65)
                    DrawCentered("Press ENTER or click to start", sw / 2, sh - 120, 26, RAYWHITE);
                break;
            case Screen::Playing:
                renderer.DrawHud(*world, sw, sh, feelOn, best);
                break;
            case Screen::Paused:
                renderer.DrawHud(*world, sw, sh, feelOn, best);
                DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.6f));
                DrawCentered("PAUSED", sw / 2, sh / 5, 64, RAYWHITE);
                DrawControls(sw / 2, sh / 5 + 100);
                DrawCentered("Esc resume   Q quit to title", sw / 2, sh - 120, 22, RAYWHITE);
                break;
            case Screen::GameOver:
                renderer.DrawHud(*world, sw, sh, feelOn, best);
                DrawRectangle(0, 0, sw, sh, Fade(BLACK, 0.55f));
                std::snprintf(buf, sizeof(buf), "DOWN ON WAVE %d", world->Wave());
                DrawCentered(buf, sw / 2, sh / 3, 64, Color{255, 110, 100, 255});
                std::snprintf(buf, sizeof(buf), "Score %d   Kills %d   Best %d", world->Score(), world->Kills(), best);
                DrawCentered(buf, sw / 2, sh / 3 + 84, 28, RAYWHITE);
                DrawCentered("R restart   Esc title", sw / 2, sh / 3 + 140, 22, Color{190, 196, 210, 255});
                break;
        }
        if (debug && screen != Screen::Title) renderer.DrawDebugPanel(*world, stats, sw, sh);
        if (toastTime > 0.0f) {
            toastTime -= frameDt;
            DrawCentered(toast.c_str(), sw / 2, sh - 60, 20, Fade(RAYWHITE, std::min(1.0f, toastTime)));
        }

        if (!opt.recordDir.empty() || !opt.shots.empty()) {
            std::string path;
            if (!opt.recordDir.empty()) path = opt.recordDir + "/" + TextFormat("frame_%05d.png", frame);
            for (const auto& [f, p] : opt.shots)
                if (f == frame) path = p;
            if (!path.empty()) {
                rlDrawRenderBatchActive();  // flush queued HUD draws before reading the framebuffer
                Image img = LoadImageFromScreen();
                ExportImage(img, path.c_str());
                UnloadImage(img);
            }
        }
        EndDrawing();

        stats.Push(GetFrameTime() * 1000.0f);
        stats.allocsThisFrame = alloc::Count() - allocsBefore;
        if (screen == Screen::Playing && world->Phase() != WavePhase::Intermission)
            stats.allocsMaxDuringWave = std::max(stats.allocsMaxDuringWave, stats.allocsThisFrame);

        if (opt.frames > 0 && frame >= opt.frames) break;
    }

    audio.Shutdown();
    CloseWindow();
    return 0;
}
