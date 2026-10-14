#include "ichor/stage0/swarm.h"

#include "ichor/core/dmath.h"

#include <glm/geometric.hpp>

#include <algorithm>
#include <cmath>

namespace ichor::stage0 {

const char* name(SwarmState state)
{
    switch (state) {
    case SwarmState::Unaware: return "Unaware";
    case SwarmState::Approach: return "Approach";
    case SwarmState::Surround: return "Surround";
    case SwarmState::Strike: return "Strike";
    case SwarmState::Recover: return "Recover";
    }
    return "?";
}

namespace {

bool finite(glm::vec2 v)
{
    return std::isfinite(v.x) && std::isfinite(v.y);
}

void enter(SwarmBrain& brain, SwarmState next)
{
    brain.state = static_cast<std::uint8_t>(next);
    brain.stateTicks = 0;
}

glm::vec2 walkToward(glm::vec2 from, glm::vec2 to, const SwarmTuning& tuning)
{
    const glm::vec2 delta = to - from;
    const float len = glm::length(delta);
    if (len <= 1e-4f) {
        return glm::vec2(0.0f);
    }
    const float speed = std::min(tuning.walkSpeed, len / tuning.tickSeconds);
    return delta / len * speed;
}

float distanceSq(glm::vec2 a, glm::vec2 b)
{
    const glm::vec2 d = a - b;
    return d.x * d.x + d.y * d.y;
}

}

TokenResult assignAttackTokens(const std::vector<SwarmUnit>& units, glm::vec2 target, const SwarmTuning& tuning, std::int32_t ticksSinceGrant)
{
    TokenResult result;
    if (!finite(target)) {
        return result;
    }
    std::vector<const SwarmUnit*> live;
    for (const SwarmUnit& u : units) {
        if (u.alive && finite(u.pos)) live.push_back(&u);
    }
    std::sort(live.begin(), live.end(), [](const SwarmUnit* a, const SwarmUnit* b) {
        if (a->id != b->id) return a->id < b->id;
        if (a->pos.x != b->pos.x) return a->pos.x < b->pos.x;
        if (a->pos.y != b->pos.y) return a->pos.y < b->pos.y;
        if (a->holdsToken != b->holdsToken) return a->holdsToken;
        return a->cooldownTicks < b->cooldownTicks;
    });
    live.erase(std::unique(live.begin(), live.end(), [](const SwarmUnit* a, const SwarmUnit* b) { return a->id == b->id; }), live.end());

    const std::size_t cap = static_cast<std::size_t>(std::max(tuning.maxAttackers, 0));
    for (const SwarmUnit* u : live) {
        if (u->holdsToken && result.holders.size() < cap) result.holders.push_back(u->id);
    }
    if (result.holders.size() >= cap || ticksSinceGrant < tuning.tokenGapTicks) {
        return result;
    }

    const SwarmUnit* best = nullptr;
    float bestDistance = 0.0f;
    const float rangeSq = tuning.tokenRange * tuning.tokenRange;
    for (const SwarmUnit* u : live) {
        if (u->holdsToken || u->cooldownTicks > 0) continue;
        const float d = distanceSq(u->pos, target);
        // Strict less-than over ascending ids: the lower id wins a tie.
        if (d <= rangeSq && (best == nullptr || u->strikes < best->strikes || (u->strikes == best->strikes && d < bestDistance))) {
            best = u;
            bestDistance = d;
        }
    }
    if (best != nullptr) {
        result.holders.insert(std::lower_bound(result.holders.begin(), result.holders.end(), best->id), best->id);
        result.granted = true;
        result.newHolder = best->id;
    }
    return result;
}

glm::vec2 surroundSlot(int rank, int count, glm::vec2 target, float radius)
{
    if (count <= 0) {
        return target;
    }
    const float angle = dmath::kTwoPi * static_cast<float>(rank) / static_cast<float>(count);
    return target + glm::vec2(dmath::cos(angle), dmath::sin(angle)) * radius;
}

SwarmIntent swarmStep(SwarmBrain& brain, const SwarmSenses& senses, const SwarmTuning& tuning)
{
    SwarmIntent intent;
    if (brain.cooldownTicks > 0) {
        brain.cooldownTicks -= 1;
    }
    if (!senses.targetAlive) {
        // A dead target is not an opponent.
        if (brain.swarmState() != SwarmState::Unaware) enter(brain, SwarmState::Unaware);
        brain.token = false;
        return intent;
    }
    if (!finite(senses.self) || !finite(senses.target) || !finite(senses.slot)) {
        return intent;
    }
    brain.stateTicks += 1;
    const float distance = glm::length(senses.target - senses.self);

    switch (brain.swarmState()) {
    case SwarmState::Unaware:
        if (distance <= tuning.aggroRange) enter(brain, SwarmState::Approach);
        break;

    case SwarmState::Approach:
        intent.move = walkToward(senses.self, senses.slot, tuning);
        if (distance <= tuning.surroundRadius + 0.5f) enter(brain, SwarmState::Surround);
        break;

    case SwarmState::Surround:
        if (brain.token && senses.mayStrike && distance <= tuning.strikeRange) {
            enter(brain, SwarmState::Strike);
        } else if (brain.token) {
            intent.move = walkToward(senses.self, senses.target, tuning);
        } else {
            intent.move = walkToward(senses.self, senses.slot, tuning);
        }
        break;

    case SwarmState::Strike:
        // Committed: no cancelling, no repositioning.
        if (brain.stateTicks >= tuning.windupTicks) {
            intent.strike = true;
            brain.strikes += 1;
            enter(brain, SwarmState::Recover);
        }
        break;

    case SwarmState::Recover:
        if (brain.stateTicks >= tuning.recoveryTicks) {
            brain.token = false;
            brain.cooldownTicks = tuning.attackCooldownTicks;
            enter(brain, SwarmState::Surround);
        }
        break;
    }
    return intent;
}

SwarmEvents tickSwarm(SwarmGroup& group, SwarmTarget target, const SwarmTuning& tuning)
{
    SwarmEvents events;
    std::vector<std::size_t> order;
    for (std::size_t i = 0; i < group.members.size(); ++i) {
        if (group.members[i].alive) order.push_back(i);
    }
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        const SwarmMember& l = group.members[a];
        const SwarmMember& r = group.members[b];
        if (l.id != r.id) return l.id < r.id;
        if (l.pos.x != r.pos.x) return l.pos.x < r.pos.x;
        return l.pos.y < r.pos.y;
    });

    if (group.ticksSinceGrant < (1 << 20)) group.ticksSinceGrant += 1;
    if (target.alive) {
        std::vector<SwarmUnit> units;
        for (const SwarmMember& m : group.members) {
            units.push_back({m.id, m.pos, m.alive, m.brain.token, m.brain.cooldownTicks, m.brain.strikes});
        }
        const TokenResult tokens = assignAttackTokens(units, target.pos, tuning, group.ticksSinceGrant);
        for (std::size_t i : order) {
            SwarmMember& m = group.members[i];
            m.brain.token = std::binary_search(tokens.holders.begin(), tokens.holders.end(), m.id);
        }
        if (tokens.granted) {
            group.ticksSinceGrant = 0;
            events.granted.push_back(tokens.newHolder);
        }
    }

    const int count = static_cast<int>(order.size());
    bool struckStart = false;
    for (int rank = 0; rank < count; ++rank) {
        SwarmMember& m = group.members[order[static_cast<std::size_t>(rank)]];
        SwarmSenses senses;
        senses.self = m.pos;
        senses.target = target.pos;
        senses.slot = surroundSlot(rank, count, target.pos, tuning.surroundRadius);
        senses.targetAlive = target.alive;
        senses.mayStrike = !struckStart;
        const SwarmState before = m.brain.swarmState();
        const SwarmIntent intent = swarmStep(m.brain, senses, tuning);
        if (before != SwarmState::Strike && m.brain.swarmState() == SwarmState::Strike) struckStart = true;
        if (intent.strike) events.strikes.push_back(m.id);
        m.pos += intent.move * tuning.tickSeconds;
    }
    return events;
}

}
