#pragma once

#include "ichor/core/reflect.h"

#include <glm/vec2.hpp>

#include <cstdint>

namespace ichor::stage0 {

// Ambusher: waits in concealment and strikes from it. All timing is whole ticks.
// The target can spot it (close enough and looking at its cover), which costs it
// the short ambush wind-up but not its place in the fight.
struct AmbusherTuning {
    float triggerRange = 3.5f;    // target this close to the cover point springs the ambush
    float spotRange = 7.0f;       // beyond this the target cannot see through the concealment
    float spotCosHalfCone = 0.8f; // target facing must be at least this aligned with the cover to count as looking
    int spotTicksNeeded = 45;     // ticks of looking before the cover is blown
    int spotDecayTicks = 1;       // spotting progress lost per tick spent not looking
    int ambushWindupTicks = 12;   // a sprung ambush is fast
    int windupTicks = 30;         // a blown ambush gives the same tell as any other attack
    int recoveryTicks = 45;
    int rushMaxTicks = 120;       // gives up the chase and withdraws
    int withdrawMaxTicks = 300;   // blocked on the way home: hides where it stands rather than never re-arming
    int rearmTicks = 240;         // hidden again, but deaf to triggers for this long
    float strikeRange = 1.4f;
    float rushSpeed = 6.0f;
    float withdrawSpeed = 3.0f;
    float arriveRadius = 0.1f;
    float tickSeconds = 1.0f / 60.0f;
};

enum class AmbusherState : std::uint8_t { Hidden, Rush, Strike, Recover, Withdraw };
const char* name(AmbusherState state);

struct AmbusherBrain {
    std::uint8_t state = 0;
    std::int32_t stateTicks = 0;
    std::int32_t cooldownTicks = 0;
    std::int32_t spotTicks = 0;
    bool concealed = true; // false once the target has looked at the cover long enough
    bool ambush = true;    // the current attack was sprung from concealment
    std::int32_t strikes = 0;

    AmbusherState ambusherState() const { return static_cast<AmbusherState>(state); }
};

struct AmbusherSenses {
    glm::vec2 self{0.0f};
    glm::vec2 cover{0.0f};
    glm::vec2 target{0.0f};
    glm::vec2 targetFacing{0.0f, -1.0f}; // need not be unit length; zero or non-finite means looking at nothing
    bool targetAlive = true;
};

struct AmbusherIntent {
    glm::vec2 move{0.0f}; // metres per second
    bool strike = false;  // the wind-up finished this tick
    bool ambush = false;  // with `strike`: it was sprung from concealment
};

// Pure: one decision per fixed tick. Non-finite position senses change nothing, and
// an unknown state byte or runaway counter in a loaded brain is pulled back in.
AmbusherIntent ambusherStep(AmbusherBrain& brain, const AmbusherSenses& senses, const AmbusherTuning& tuning);

}

namespace ichor {

template <>
struct Reflect<stage0::AmbusherBrain> {
    static constexpr auto fields()
    {
        using B = stage0::AmbusherBrain;
        return std::make_tuple(field("state", &B::state), field("stateTicks", &B::stateTicks), field("cooldownTicks", &B::cooldownTicks),
            field("spotTicks", &B::spotTicks), field("concealed", &B::concealed), field("ambush", &B::ambush), field("strikes", &B::strikes));
    }
};

}
