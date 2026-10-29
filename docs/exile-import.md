# The Exile in the engine

`art/exile/SK_Exile.glb` (rig, textured mesh, nine clips), `SM_ExileSpear.glb` and `SM_ExileRoundShield.glb` go
through the ordinary glTF cooker. `ichor/anim/exile.h` (`cookExile`) cooks the three files and works out where the
props are held. Nothing else is special: no source-format importer.

Run it in the app with `ichor --world "" --exile art/exile` (a flag, off by default; `--world ""` skips terrain
streaming, which otherwise holds the simulation back): an Idle and a Walk Exile stand in the arena, each carrying
the spear and the shield.

## What the GLB contains (measured)

- Metres, Y up, about 2 m tall, feet on y = 0. Joint 0 is a `root` bone above `mixamorig:Hips`, so the engine's root
  motion (the scene subtracts the root joint's translation) works as it is: 35 joints, root plus 34 bones.
- The body faces **+Z**. Ichor's characters face -Z (`character_pose.h`). `CookOptions::rootYawDegrees` turns every
  root joint half a way round about the vertical, in the skeleton and in the clips, and turns its translation (the
  root motion) with it, so travel along the way the body faces stays along it. `cookExile` passes 180. The mesh and the inverse bind matrices are untouched.
- Clips: `block death dodge hit idle run swing thrust walk`, lowercase, 24 fps keys resampled at 60 Hz by the cooker.

## Root motion

Clips are authored so the root travels the way the legs plant their feet, along the way the body faces (+Z in the file, -Z after the turn). Walk is a loop of 2.7 s at about 1.05 m/s (2.86 m per loop) and Run a loop of 1.75 s at about 5.2 m/s (9.1 m per loop). The planted ankle moves at about 0.07 m/s in Walk.

## Loops

Idle, Walk and Run loop: the last frame is a copy of the first (hips height included) and the root moves a constant
forward distance per loop. Hit, Swing and Death end far from where they start, as one-shots do. `idle, walk and run
loop` in `test_animation_exile.cpp` checks the seam.

## Bone roll (SEED-717)

`art/blender/export_glb.py` copies each clip onto the mesh rig by joint direction only ("swing only", in
`target_rotations`): a bone's roll about its own axis followed the bind pose, not the clip. Joint directions were
right but rolls were not, by up to 180 degrees, and a twist about the spine (the torso turning in Swing and Death)
was lost altogether. So the skinned elbows bent against the upper arm, the forearms wrapped up to 156 degrees, and
joints sat up to 156 mm (fingers) and 125 mm (shoulders) from where the source clip puts them. The Blender script
still does this for any future export (SEED-739).

`tools/restore_exile_roll.py` puts the roll back without Blender: it reads the `SK_Exile_<Clip>.fbx` files (binary FBX,
plain Python) and rewrites the rotation keys of every `mixamorig` bone in `SK_Exile.glb`. Translations, the `root`
bone, the mesh and the skin are untouched. The map: `SK_Exile.fbx`'s BindPose gives every joint's bind rotation in the
FBX world and the GLB's node rest gives it in the GLB, and one fixed rotation takes the first onto the second for all
joints (largest error 0.0005 degrees; the script stops above 0.01). A clip's world rotation from its Lcl curves,
turned by that map, is the GLB world rotation. Afterwards every joint of every clip sits where the source FBX puts it,
to 0.0 mm (relative to the hips). Same input bytes give the same output bytes, and a second run changes nothing.

## Arm solve

The source clips straighten the elbows past straight (Idle, Block, Hit, by up to 42 degrees) and turn the forearm up to
146 degrees about its bone against the upper arm (Thrust, Swing, Death, Run). `cookExile` gives the cooker a clip pass
(`CookOptions::clipPass`, run on the 60 Hz samples before compression) that solves each arm again per sample as a
two-bone chain, from the posed skeleton only:

- The shoulder and the hand stay where the source puts them (a hand moves only where the reach asks for a bend under
  14 degrees or over 150).
- The elbow goes to the side of the shoulder-hand line that a pole points to: outward, a little back and down, in the
  frame of the shoulders and the neck. The elbow never locks straight: flex is at least 14 degrees, which stands the
  upper arm out from the ribs by about 7 degrees beyond the hand line (a larger abduction would move the hand).
- The elbow is a pure hinge, about the axis the bind pose bends about, carried by the upper arm. The upper arm's roll
  follows the plane of the pole, so the source's upper-arm roll is not kept.
- The hand keeps its world rotation. Its turn about the forearm bone against the bind is unwrapped along the clip and
  saturated by `45 * tanh(t / 45)` degrees: smooth, no wrap, under 45.

## Sockets

The props' origins sit at the grip (spear) and the middle (shield). `heldMatrix` (`ichor/anim/exile.h`) gives each
prop's draw matrix for a pose, and every consumer (`ExilePlayer`, `ExileAvatar`, the `--exile` demo) draws through it.

- The spear is held in `mixamorig:RightHand`, upright as the hand stands at the first frame of Idle. From there the
  hand carries it: the draw matrix is the joint's skinning matrix times `JointSocket::local`.
- The shield is centred on `mixamorig:LeftHand`, and each pose turns its outer face (+Y in `SM_ExileRoundShield.glb`)
  to the way the hips face: forward is square to the hips' left-right (the up-legs) and the spine's up, and the
  shield's up is the spine's up made square to that. It reads only the posed skeleton, so it is the same at any frame
  rate and follows a crossfade through the blended skin, with no state between frames. A fixed grip cannot do this
  with the restored roll: in Block's guard the left forearm points at the threat, and the best fixed offset on the
  hand or the forearm faces 54 degrees off forward in both Idle and the guard (SEED-717). The spine gives the up, not
  the forearm, because in the guard the forearm lies nearly along the face.

`applyAttachment` (`ichor/sim/attachment.h`) moves a prop entity to the draw matrix each tick in the `--exile` demo,
the only place the props are simulation entities. Combat, Stage 0 and the bridge only draw them.

## Placeholder status

Props untextured, one take per clip.
