#include "arena/EnemyBrain.h"

namespace arena {
namespace {

// Ranged enemies stop tracking this long before they fire, so a sidestep at
// the last moment dodges the shot.
constexpr float kRangedAimLock = 0.18f;
// Minimum time a ranged enemy repositions between shots.
constexpr float kRangedReload = 0.9f;
// How far either side of its preferred range a ranged enemy is content.
constexpr float kRangedBand = 50.0f;

void Enter(Enemy& e, EnemyState s) {
    e.state = s;
    e.stateTime = 0.0f;
}

}  // namespace

const char* ToString(EnemyState s) {
    switch (s) {
        case EnemyState::Spawning: return "spawning";
        case EnemyState::Idle: return "idle";
        case EnemyState::Chase: return "chase";
        case EnemyState::Windup: return "windup";
        case EnemyState::Attack: return "attack";
        case EnemyState::Recover: return "recover";
        case EnemyState::Stagger: return "stagger";
    }
    return "?";
}

bool IsTelegraphing(const Enemy& e) { return e.state == EnemyState::Windup; }

void StaggerEnemy(Enemy& e, const EnemyDef& def) {
    if (e.state == EnemyState::Spawning) return;
    Enter(e, EnemyState::Stagger);
    e.poise = def.poise;
}

void UpdateEnemyBrain(Enemy& e, const EnemyDef& def, const BrainContext& ctx, float dt, BrainOutput& out) {
    out = BrainOutput{};
    e.stateTime += dt;

    const Vec2 toPlayer = ctx.playerPos - e.pos;
    const float dist = Length(toPlayer);
    const Vec2 dirToPlayer = dist > 1e-4f ? toPlayer / dist : e.attackDir;
    const float gap = dist - def.radius - ctx.playerRadius;  // edge to edge

    switch (e.state) {
        case EnemyState::Spawning:
            if (e.stateTime >= ctx.spawnTime) Enter(e, ctx.playerAlive ? EnemyState::Chase : EnemyState::Idle);
            break;

        case EnemyState::Idle:
            if (ctx.playerAlive) Enter(e, EnemyState::Chase);
            break;

        case EnemyState::Chase: {
            if (!ctx.playerAlive) {
                Enter(e, EnemyState::Idle);
                break;
            }
            if (def.kind == EnemyKind::Ranged) {
                if (dist > def.preferredRange + kRangedBand) out.desiredVel = dirToPlayer * def.speed;
                else if (dist < def.preferredRange - kRangedBand) out.desiredVel = -dirToPlayer * def.speed;
                else out.desiredVel = Perp(dirToPlayer) * (def.speed * 0.6f * e.strafeSign);

                if (gap <= def.attackRange && e.stateTime >= kRangedReload) {
                    Enter(e, EnemyState::Windup);
                    e.attackDir = dirToPlayer;
                    out.startedWindup = true;
                }
            } else {
                out.desiredVel = dirToPlayer * def.speed;
                if (gap <= def.attackRange) {
                    Enter(e, EnemyState::Windup);
                    e.attackDir = dirToPlayer;  // locked from here on
                    out.startedWindup = true;
                }
            }
            break;
        }

        case EnemyState::Windup:
            if (def.kind == EnemyKind::Ranged && e.stateTime < def.windup - kRangedAimLock) e.attackDir = dirToPlayer;
            if (e.stateTime >= def.windup) {
                Enter(e, EnemyState::Attack);
                e.attackLanded = false;
                out.beginAttack = true;
            }
            break;

        case EnemyState::Attack:
            if (def.kind == EnemyKind::Melee) {
                out.desiredVel = e.attackDir * def.lungeSpeed;
                out.snapVelocity = true;
            }
            if (e.stateTime >= def.active) Enter(e, EnemyState::Recover);
            break;

        case EnemyState::Recover:
            if (e.stateTime >= def.recover) {
                Enter(e, ctx.playerAlive ? EnemyState::Chase : EnemyState::Idle);
                if (def.kind == EnemyKind::Ranged) e.strafeSign = -e.strafeSign;
            }
            break;

        case EnemyState::Stagger:
            if (e.stateTime >= def.staggerTime) Enter(e, ctx.playerAlive ? EnemyState::Chase : EnemyState::Idle);
            break;
    }
}

}  // namespace arena
