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

## Sockets

The props' origins sit at the grip (spear) and the middle (shield). `cookExile` puts each on its hand (spear in
`mixamorig:RightHand`, shield in `mixamorig:LeftHand`) as the hand stands at the first frame of Idle, with the spear
upright and the shield's flat face forward. From there the hand carries it: the draw matrix is the joint's skinning
matrix times `JointSocket::local`. `applyAttachment` (`ichor/sim/attachment.h`) moves a prop entity to it each tick.

## Placeholder status

Props untextured, one take per clip.
