#include "Audio.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>

namespace game {
namespace {

constexpr int kRate = 44100;
constexpr float kTau = 6.28318530718f;

struct Noise {
    std::uint32_t s = 0x1234567u;
    float Next() {
        s = s * 1664525u + 1013904223u;
        return static_cast<float>(s >> 8) / 8388608.0f - 1.0f;  // [-1, 1)
    }
};

// Renders `seconds` of audio from `sample(t, dt)` with an attack/decay
// envelope, into a raylib Sound.
Sound Synth(float seconds, float attack, float decayPower, const std::function<float(float, float)>& sample) {
    const int frames = static_cast<int>(seconds * kRate);
    std::vector<short> pcm(static_cast<std::size_t>(frames));
    const float dt = 1.0f / kRate;
    for (int i = 0; i < frames; ++i) {
        const float t = i * dt;
        const float env = std::min(1.0f, t / std::max(attack, 1e-4f)) * std::pow(1.0f - static_cast<float>(i) / frames, decayPower);
        const float v = std::clamp(sample(t, dt) * env, -1.0f, 1.0f);
        pcm[static_cast<std::size_t>(i)] = static_cast<short>(v * 30000.0f);
    }
    Wave wave{};
    wave.frameCount = static_cast<unsigned int>(frames);
    wave.sampleRate = kRate;
    wave.sampleSize = 16;
    wave.channels = 1;
    wave.data = pcm.data();
    return LoadSoundFromWave(wave);  // copies the samples
}

float Square(float phase) { return std::fmod(phase, 1.0f) < 0.5f ? 1.0f : -1.0f; }

}  // namespace

bool Audio::Init() {
    InitAudioDevice();
    if (!IsAudioDeviceReady()) return false;

    auto load = [&](Id id, Sound s) {
        Voices& v = m_sounds[id];
        v.base = s;
        for (auto& a : v.alias) a = LoadSoundAlias(s);
        v.loaded = true;
    };

    {   // Swing: a quick band of filtered noise that sweeps down.
        Noise n;
        float lp = 0;
        load(Swing, Synth(0.09f, 0.005f, 2.0f, [&](float t, float) {
            const float cutoff = 0.35f - t * 3.0f;
            lp += (n.Next() - lp) * std::max(0.05f, cutoff);
            return lp * 0.8f;
        }));
    }
    {   // Finisher: heavier, lower swoosh.
        Noise n;
        float lp = 0;
        load(Finisher, Synth(0.16f, 0.01f, 1.6f, [&](float t, float) {
            lp += (n.Next() - lp) * std::max(0.03f, 0.22f - t * 1.2f);
            return lp * 1.1f;
        }));
    }
    {   // Hit: a thump with a click on top.
        Noise n;
        float phase = 0;
        load(Hit, Synth(0.11f, 0.001f, 2.5f, [&](float t, float dt) {
            phase += (180.0f - t * 900.0f) * dt;
            return 0.7f * std::sin(kTau * phase) + (t < 0.01f ? 0.5f * n.Next() : 0.0f);
        }));
    }
    {   // Player shot: short downward chirp.
        float phase = 0;
        load(Shot, Synth(0.07f, 0.002f, 1.5f, [&](float t, float dt) {
            phase += (1100.0f - t * 9000.0f) * dt;
            return 0.35f * Square(phase);
        }));
    }
    {   // Enemy shot: softer upward blip.
        float phase = 0;
        load(EnemyShot, Synth(0.12f, 0.004f, 1.2f, [&](float t, float dt) {
            phase += (380.0f + t * 1600.0f) * dt;
            return 0.4f * std::sin(kTau * phase);
        }));
    }
    {   // Dash: airy whoosh.
        Noise n;
        float lp = 0;
        load(Dash, Synth(0.18f, 0.03f, 1.4f, [&](float t, float) {
            lp += (n.Next() - lp) * (0.08f + 0.25f * std::sin(t * 18.0f) * std::sin(t * 18.0f));
            return lp * 0.9f;
        }));
    }
    {   // Hurt: buzzy drop.
        float phase = 0;
        load(Hurt, Synth(0.25f, 0.002f, 1.3f, [&](float t, float dt) {
            phase += (240.0f - t * 500.0f) * dt;
            return 0.45f * (2.0f * std::fmod(phase, 1.0f) - 1.0f);
        }));
    }
    {   // Kill: descending pop.
        float phase = 0;
        load(Kill, Synth(0.16f, 0.002f, 1.8f, [&](float t, float dt) {
            phase += (720.0f - t * 3500.0f) * dt;
            return 0.4f * Square(phase) * 0.6f + 0.3f * std::sin(kTau * phase * 0.5f);
        }));
    }
    {   // Slam: low boom.
        Noise n;
        float lp = 0, phase = 0;
        load(Slam, Synth(0.4f, 0.002f, 1.6f, [&](float t, float dt) {
            lp += (n.Next() - lp) * 0.04f;
            phase += (70.0f - t * 60.0f) * dt;
            return 1.4f * lp + 0.8f * std::sin(kTau * phase);
        }));
    }
    {   // Windup: a quiet rising tick, so telegraphs can be heard as well as seen.
        float phase = 0;
        load(Windup, Synth(0.08f, 0.01f, 1.0f, [&](float t, float dt) {
            phase += (300.0f + t * 2500.0f) * dt;
            return 0.18f * std::sin(kTau * phase);
        }));
    }
    {   // Wave start: two-note horn.
        float phase = 0;
        load(WaveStart, Synth(0.5f, 0.02f, 0.8f, [&](float t, float dt) {
            phase += (t < 0.2f ? 220.0f : 330.0f) * dt;
            return 0.3f * (Square(phase) * 0.5f + std::sin(kTau * phase) * 0.5f);
        }));
    }
    {   // Wave cleared: rising arpeggio.
        float phase = 0;
        load(WaveClear, Synth(0.45f, 0.01f, 0.9f, [&](float t, float dt) {
            const float f = t < 0.12f ? 523.0f : (t < 0.24f ? 659.0f : 784.0f);
            phase += f * dt;
            return 0.28f * std::sin(kTau * phase);
        }));
    }
    {   // Shot hitting a wall.
        Noise n;
        load(Wall, Synth(0.04f, 0.001f, 3.0f, [&](float, float) { return 0.3f * n.Next(); }));
    }

    m_ready = true;
    return true;
}

void Audio::Shutdown() {
    if (!m_ready) return;
    for (auto& v : m_sounds) {
        if (!v.loaded) continue;
        for (auto& a : v.alias) UnloadSoundAlias(a);
        UnloadSound(v.base);
    }
    CloseAudioDevice();
    m_ready = false;
}

void Audio::Play(Id id, float volume, float pitch) {
    if (!m_ready || m_muted) return;
    Voices& v = m_sounds[id];
    if (!v.loaded) return;
    Sound& s = v.alias[static_cast<std::size_t>(v.next)];
    v.next = (v.next + 1) % kVoices;
    m_seed = m_seed * 1103515245u + 12345u;
    const float jitter = 0.94f + 0.12f * static_cast<float>((m_seed >> 16) & 0x7FFF) / 32767.0f;  // avoid machine-gun repeats
    SetSoundVolume(s, volume);
    SetSoundPitch(s, pitch * jitter);
    PlaySound(s);
}

void Audio::OnEvents(const arena::World& world) {
    using arena::EventType;
    const arena::Vec2 listener = world.Player().pos;
    for (const auto& e : world.Events()) {
        // Quieter the further from the player, so off-screen fights don't drown things out.
        const float near = std::clamp(1.2f - arena::Distance(e.pos, listener) / 900.0f, 0.35f, 1.0f);
        switch (e.type) {
            case EventType::PlayerSwing: Play(e.value >= 2.0f ? Finisher : Swing, 0.55f, 1.0f); break;
            case EventType::EnemyHit: Play(Hit, 0.7f * near, 1.0f); break;
            case EventType::PlayerShot: Play(Shot, 0.25f, 1.0f); break;
            case EventType::EnemyAttack:
                if (e.enemyDef >= 0 && world.Data().enemies[static_cast<std::size_t>(e.enemyDef)].kind == arena::EnemyKind::Ranged)
                    Play(EnemyShot, 0.45f * near, 1.0f);
                break;
            case EventType::PlayerDash: Play(Dash, 0.5f, 1.0f); break;
            case EventType::PlayerHurt: Play(Hurt, 0.8f, 1.0f); break;
            case EventType::PlayerDied: Play(Slam, 0.9f, 0.6f); break;
            case EventType::EnemyKilled: Play(Kill, 0.5f * near, 1.0f); break;
            case EventType::Slam: Play(Slam, 0.9f * near, 1.0f); break;
            case EventType::EnemyWindup: Play(Windup, 0.5f * near, 1.0f); break;
            case EventType::WaveStarted: Play(WaveStart, 0.5f, 1.0f); break;
            case EventType::WaveCleared: Play(WaveClear, 0.5f, 1.0f); break;
            case EventType::ProjectileHitWall: Play(Wall, 0.25f * near, 1.0f); break;
            default: break;
        }
    }
}

}  // namespace game
