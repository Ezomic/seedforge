# Art pipeline: node names the cooker understands

`ichor_cook` turns a glTF or GLB into a `.iscene` bundle. Most of what it does needs no help from the art side: every node that carries a mesh becomes a placed piece. Three conventions go beyond that. They are read from **node names**, so they work from Blender with nothing but renaming. A file that uses none of them cooks exactly as before.

A **piece** is a mesh node that stays placed, named by the art side (`SM_Foo`). Sockets and collision belong to a piece, and LODs are versions of one.

## Sockets: `SOCKET_<name>`

An **empty** node named `SOCKET_<name>` is kept as a named transform instead of being dropped.

- Parent it to the piece's mesh node. The socket is then stored relative to that piece (position and rotation, scale is ignored), so it follows the piece wherever the piece is placed.
- An empty at the scene root belongs to no piece and is stored in file space.
- `<name>` is whatever follows the prefix: `SOCKET_hand_r` is the socket `hand_r`. No whitespace. Names are case sensitive and unique per piece; the prefix must be exactly `SOCKET_` in capitals.
- The node must be an empty. A `SOCKET_` node that carries a mesh stays an ordinary piece.

Engine side: `findSocket(bundle, "SM_Foo", "hand_r")` returns the socket (`position`, `rotation` as a quaternion) or `nullptr`. A file-level socket is found by any piece name when the piece has none of that name.

## LODs: `<piece>_LOD0`, `_LOD1`, `_LOD2`

Give the meshes of one piece the suffix `_LOD0`, `_LOD1`, `_LOD2` (`SM_Foo_LOD0`, `SM_Foo_LOD1`, ...). The cooker builds one LOD group named `SM_Foo`.

- Levels start at `_LOD0` and follow without gaps; a group has at least two levels and at most three.
- `_LOD0` is the node that is placed (it keeps its full name). The other levels are cooked as meshes and are not placed; the renderer swaps to them by distance.
- Each level has a switch distance, stored in the bundle. It is measured from the bounding radius `r` of `_LOD0` (at least 0.5 m): `_LOD1` takes over at `16 r`, `_LOD2` at `40 r`. A 2 m wide piece switches at 16 m and 40 m.
- Sockets and collision of a piece are named after the piece without the suffix (`SM_Foo`), whichever LOD node they hang from.
- A malformed group (no `_LOD0`, a gap, a second mesh for one level, `_LOD3` or higher) is reported and its meshes stay ordinary placed pieces, so nothing disappears.

Engine side: `findLodGroup(bundle, "SM_Foo")` then `lodMeshAt(group, distance)` gives the mesh index to draw.

## Collision: `UBX_<piece>` and `UCX_<piece>`

A mesh node named `UBX_<piece>` (box) or `UCX_<piece>` (convex) is a collision shape for `<piece>`. It is **not rendered**: it is not cooked as a mesh and is not placed.

- `UBX_`: the cooker stores the axis-aligned box of its vertices in the piece's space (`boundsMin`, `boundsMax`). Model it as a box.
- `UCX_`: the vertices are stored as the hull candidates (`points`, piece space) together with their bounds. The cooker does not build the hull; the physics side does.
- `<piece>` must be the name of a placed piece in the same file. If it is not, the shape is reported and kept in file space.
- Several shapes may belong to one piece.

Engine side: `bundle.collision` lists the shapes with their `piece`.

## Checking a file before it goes in

```
ichor_cook --validate art/settlement_kit/SM_Foo.glb [more files ...]
```

For each file this prints how many sockets, LOD groups and collision shapes it holds, one line for each, and one `issue` line for every name that breaks the rules above (empty socket name, `socket_` in lower case, duplicate socket, `_LOD5`, a lone `_LOD1`, `UCX_` without a mesh, a collision shape for a piece that is not there). It reads the file and cooks nothing, and exits 1 only when a file cannot be read. The same issues are logged as warnings by a normal cook.

## What a bundle looks like

A bundle without any of these nodes is byte for byte what the cooker wrote before. One that has them carries an extra block after the nodes, tagged `IPC1`: sockets, LOD groups, collision shapes. The format version stays 3.
