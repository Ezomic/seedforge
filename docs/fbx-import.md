# FBX character import

Ichor's cooker reads glTF. The Exile (`art/exile`) arrived as binary FBX that Blender wrote from a glTF, so
`engine/assets/src/fbx.cpp` reads binary FBX (7100 to 7700, zlib arrays) and `fbx_import.cpp` turns a skinned
character into the same bundle types the glTF cooker fills. `exile_cook.cpp` cooks the whole kit. Nothing at
runtime knows the source was an FBX.

Run it in the app with `ichor --exile art/exile` (a flag, off by default): an Idle and a Walk Exile stand in the
arena, each carrying the spear and the shield.

## What the files contain

- Y up, `UnitScaleFactor` 1 (one unit is a centimetre). Blender's usual layout: the armature and mesh nodes carry a
  -90 degree X turn and a scale of 100, and the vertex data is still in Blender metres. Multiplying everything by
  `UnitScaleFactor * 0.01` makes the node scale of 100 cancel, leaving the character in metres.
- The mesh is centred on its bounding box (feet at -1, head top at +1), so the importer lifts the model until the
  lowest vertex sits on y = 0.
- The character faces +Z. Ichor characters face -Z, so the whole model turns by 180 degrees about Y
  (`FbxImportOptions::yawDegrees`).
- Bind poses come from the skin clusters (`TransformLink`), not from the `Lcl` values, which hold frame zero of
  whatever pose Blender had when it exported. Clips come from the `Lcl` animation curves of each clip file and are
  resampled at 60 Hz.

## Root motion

The clips animate the hips bone with its travel in the keys. The importer inserts a joint named `root` above the
hips and moves the horizontal travel (relative to frame zero) onto it, so `AnimationSet::rootTranslation` and
`Scene`'s root motion work unchanged. The hips keep their height and their turning. `root` has no rotation or
scale and its y is zero, which is what the scene's `m[3] -= root` step assumes.

Measured travel (after conversion): Walk about 4.2 m over 4.0 s, Run about 15.4 m over 3.0 s (5.2 m/s).

## The root travel runs against the pose

In Walk and Run the legs plant their feet for a body that moves along +Z (the way the mesh faces in the file), but
the hips travel along -Z. Played as authored the feet slide at about twice the walking speed. The importer
therefore keeps the authored travel direction (-Z, Ichor's forward) and turns the body to face it, which is the
same as reversing the travel against the pose. `FbxImportOptions::reverseRootTravel` switches this off; the
tests assert that the planted-foot speed is far lower with it on, so they go red if it is removed.
`yawDegrees = 0` leaves the body facing +Z, which the tests also catch.

This is a defect in the placeholder art (reported on SEED-470), not a convention: a re-export with the travel
matching the pose should not need the option.

## Sockets

A prop is baked at the placement it has in its own file (through the same conversion), so the skinning matrix of
one joint moves it as if it were rigidly skinned to that joint. `JointSocket` names that joint; `applyAttachment`
(`ichor/sim/attachment.h`) moves a prop entity to it each tick. The hand is whichever of the two hand bones is
nearest the prop's centre in the bind pose.

## Placeholder status

Untextured, one take per clip, not blended to loop. Hips height at the last frame does not match the first in
Walk and Run, so looping them pops; Idle's seam is small.
