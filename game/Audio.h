#pragma once
// Sound effects, synthesized at startup (no audio files to ship). Each effect
// keeps a few aliases of the same buffer so rapid hits overlap instead of
// cutting each other off.
#include <array>
#include <vector>

#include <raylib.h>

#include "arena/World.h"

namespace game {

class Audio {
public:
    bool Init();
    void Shutdown();
    void OnEvents(const arena::World& world);  // call after each World::Step
    void SetMuted(bool m) { m_muted = m; }
    bool Muted() const { return m_muted; }

private:
    enum Id { Swing, Finisher, Hit, Shot, EnemyShot, Dash, Hurt, Kill, Slam, Windup, WaveStart, WaveClear, Wall, Count };
    static constexpr int kVoices = 4;

    struct Voices {
        Sound base{};
        std::array<Sound, kVoices> alias{};
        int next = 0;
        bool loaded = false;
    };

    void Play(Id id, float volume, float pitch);

    std::array<Voices, Count> m_sounds{};
    bool m_ready = false;
    bool m_muted = false;
    unsigned m_seed = 12345;
};

}  // namespace game
