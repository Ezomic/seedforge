#pragma once

#include "ichor/core/input.h"
#include "ichor/sim/scene.h"
#include "ichor/stage0/combat.h"

namespace ichor {

// A scripted player for the Anchor and the Artillery (SEED-661), the way FightBot is for the skirmisher. It reads what a
// person sees of the foe's telegraph (the slam's wind-up, the shell's mark) and answers through the ordinary
// InputFrame, in world axes with the player facing the foe, so it needs the bridge's legacy controls. It has no
// randomness and no reaction delay: it is a probe of whether the role can be beaten on a piece of ground, not a model
// of a player.
//   Anchor     waits outside the slam radius, goes in while the anchor is planted or recovering with time to spare,
//              and steps out again before the slam lands.
//   Artillery  sprints at it, which also takes the player off the marked point, and strikes when in reach.
// Any other role gets the closing-in and striking part only. Empty frames when either fighter is gone or committed.
InputFrame roleBotInput(Scene& scene, const stage0::Stage0& stage);

}
