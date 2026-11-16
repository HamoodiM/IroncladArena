#include "arena/GameData.h"

#include <cstdio>
#include <initializer_list>

#include "arena/Config.h"

namespace arena {
namespace {

struct FloatField {
    const char* key;
    float* value;
};
struct IntField {
    const char* key;
    int* value;
};
struct ListField {  // "2, 2, 4"
    const char* key;
    float* values;
    int count;
};

std::string Where(const ConfigSection& s) {
    return "line " + std::to_string(s.line) + " [" + s.type + (s.name.empty() ? "" : " " + s.name) + "]";
}

bool ParseList(std::string_view text, float* out, int count) {
    int n = 0;
    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t comma = text.find(',', pos);
        const std::string_view item = text.substr(pos, comma == std::string_view::npos ? std::string_view::npos : comma - pos);
        if (n >= count || !ParseFloat(item, out[n])) return false;
        ++n;
        if (comma == std::string_view::npos) break;
        pos = comma + 1;
    }
    return n == count;
}

bool ParseColor(std::string_view text, std::uint32_t& out) {
    if (text.size() != 7 || text[0] != '#') return false;
    std::uint32_t rgb = 0;
    for (char c : text.substr(1)) {
        int v;
        if (c >= '0' && c <= '9') v = c - '0';
        else if (c >= 'a' && c <= 'f') v = c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') v = c - 'A' + 10;
        else return false;
        rgb = rgb * 16 + static_cast<std::uint32_t>(v);
    }
    out = (rgb << 8) | 0xFFu;
    return true;
}

// Applies every key in `section` to the matching field. Unknown keys are an
// error: a typo in a tuning file should fail loudly, not be ignored.
bool Apply(const ConfigSection& section, std::initializer_list<FloatField> floats, std::initializer_list<IntField> ints,
           std::initializer_list<ListField> lists, std::initializer_list<const char*> handledElsewhere, std::string& error) {
    for (const auto& [key, value] : section.values) {
        bool matched = false;
        for (const auto& f : floats) {
            if (key != f.key) continue;
            matched = true;
            if (!ParseFloat(value, *f.value)) {
                error = Where(section) + ": '" + key + "' is not a number: " + value;
                return false;
            }
        }
        for (const auto& f : ints) {
            if (key != f.key) continue;
            matched = true;
            if (!ParseInt(value, *f.value)) {
                error = Where(section) + ": '" + key + "' is not an integer: " + value;
                return false;
            }
        }
        for (const auto& f : lists) {
            if (key != f.key) continue;
            matched = true;
            if (!ParseList(value, f.values, f.count)) {
                error = Where(section) + ": '" + key + "' needs " + std::to_string(f.count) + " comma-separated numbers";
                return false;
            }
        }
        for (const char* k : handledElsewhere)
            if (key == k) matched = true;
        if (!matched) {
            error = Where(section) + ": unknown key '" + key + "'";
            return false;
        }
    }
    return true;
}

bool LoadPlayer(const ConfigSection& s, PlayerTuning& p, std::string& error) {
    float arcDeg = p.meleeArcDeg;
    int maxHp = p.maxHp;
    if (!Apply(s,
               {{"radius", &p.radius}, {"max_speed", &p.maxSpeed}, {"accel", &p.accel}, {"friction", &p.friction},
                {"dash_speed", &p.dashSpeed}, {"dash_time", &p.dashTime}, {"dash_cooldown", &p.dashCooldown},
                {"dash_iframes", &p.dashIframes}, {"input_buffer", &p.inputBuffer}, {"hurt_invuln", &p.hurtInvuln},
                {"melee_range", &p.meleeRange}, {"melee_arc_deg", &arcDeg}, {"combo_window", &p.comboWindow},
                {"melee_move_scale", &p.meleeMoveScale}, {"melee_lunge", &p.meleeLunge}, {"shot_speed", &p.shotSpeed},
                {"shot_damage", &p.shotDamage}, {"shot_cooldown", &p.shotCooldown}, {"shot_radius", &p.shotRadius},
                {"shot_knockback", &p.shotKnockback}},
               {{"max_hp", &maxHp}},
               {{"melee_damage", p.meleeDamage, 3}, {"melee_knockback", p.meleeKnockback, 3}, {"melee_swing_time", p.meleeSwingTime, 3}},
               {}, error))
        return false;
    p.meleeArcDeg = arcDeg;
    p.maxHp = maxHp;
    if (p.maxHp <= 0 || p.maxSpeed <= 0 || p.radius <= 0) {
        error = Where(s) + ": max_hp, max_speed and radius must be positive";
        return false;
    }
    return true;
}

bool LoadEnemy(const ConfigSection& s, EnemyDef& e, std::string& error) {
    e.name = s.name;
    if (e.name.empty()) {
        error = Where(s) + ": an enemy section needs a name, e.g. [enemy grunt]";
        return false;
    }
    if (!Apply(s,
               {{"radius", &e.radius}, {"hp", &e.maxHp}, {"speed", &e.speed}, {"accel", &e.accel},
                {"attack_range", &e.attackRange}, {"windup", &e.windup}, {"active", &e.active}, {"recover", &e.recover},
                {"damage", &e.damage}, {"lunge_speed", &e.lungeSpeed}, {"slam_radius", &e.slamRadius},
                {"projectile_speed", &e.projectileSpeed}, {"preferred_range", &e.preferredRange}, {"poise", &e.poise},
                {"stagger_time", &e.staggerTime}, {"knockback_scale", &e.knockbackScale}},
               {{"score", &e.score}}, {}, {"kind", "color"}, error))
        return false;

    if (const std::string* kind = s.Find("kind")) {
        if (*kind == "melee") e.kind = EnemyKind::Melee;
        else if (*kind == "ranged") e.kind = EnemyKind::Ranged;
        else if (*kind == "brute") e.kind = EnemyKind::Brute;
        else {
            error = Where(s) + ": kind must be melee, ranged or brute, not '" + *kind + "'";
            return false;
        }
    }
    if (const std::string* color = s.Find("color"); color && !ParseColor(*color, e.color)) {
        error = Where(s) + ": color must look like #RRGGBB";
        return false;
    }
    if (e.maxHp <= 0 || e.radius <= 0 || e.windup < 0 || e.recover < 0) {
        error = Where(s) + ": hp and radius must be positive, windup and recover not negative";
        return false;
    }
    return true;
}

}  // namespace

int GameData::FindEnemy(std::string_view name) const {
    for (std::size_t i = 0; i < enemies.size(); ++i)
        if (enemies[i].name == name) return static_cast<int>(i);
    return -1;
}

bool LoadGameData(std::string_view text, GameData& out, std::string& error) {
    ConfigFile file;
    if (!ParseConfig(text, file, error)) return false;
    out = GameData{};

    // Enemies first, so waves can refer to them wherever they appear in the file.
    for (const auto& s : file.sections) {
        if (s.type != "enemy") continue;
        EnemyDef def;
        if (!LoadEnemy(s, def, error)) return false;
        if (out.FindEnemy(def.name) >= 0) {
            error = Where(s) + ": enemy '" + def.name + "' is defined twice";
            return false;
        }
        out.enemies.push_back(std::move(def));
    }

    for (const auto& s : file.sections) {
        if (s.type == "enemy") continue;
        if (s.type == "player") {
            if (!LoadPlayer(s, out.player, error)) return false;
        } else if (s.type == "rules") {
            if (!Apply(s, {{"intermission", &out.intermission}, {"extra_wave_hp_scale", &out.extraWaveHpScale}},
                       {{"extra_wave_count", &out.extraWaveCount}}, {}, {}, error))
                return false;
        } else if (s.type == "wave") {
            WaveDef wave;
            for (const auto& [key, value] : s.values) {
                if (key == "spawn_interval") {
                    if (!ParseFloat(value, wave.spawnInterval)) {
                        error = Where(s) + ": spawn_interval is not a number";
                        return false;
                    }
                    continue;
                }
                const int enemy = out.FindEnemy(key);
                int count = 0;
                if (enemy < 0) {
                    error = Where(s) + ": no enemy called '" + key + "'";
                    return false;
                }
                if (!ParseInt(value, count) || count <= 0) {
                    error = Where(s) + ": '" + key + "' needs a positive count";
                    return false;
                }
                wave.groups.push_back({enemy, count});
            }
            if (wave.groups.size() > 16) {
                error = Where(s) + ": a wave can list at most 16 enemy types";
                return false;
            }
            if (wave.groups.empty()) {
                error = Where(s) + ": a wave needs at least one enemy";
                return false;
            }
            out.waves.push_back(std::move(wave));
        } else {
            error = Where(s) + ": unknown section type '" + s.type + "'";
            return false;
        }
    }

    if (out.enemies.empty() || out.waves.empty()) {
        error = "the file needs at least one [enemy ...] and one [wave ...]";
        return false;
    }
    return true;
}

}  // namespace arena
