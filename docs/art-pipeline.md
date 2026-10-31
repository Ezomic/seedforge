# Art pipeline: node names the cooker understands

`ichor_cook` turns a glTF or GLB into a `.iscene` bundle. Most of what it does needs no help from the art side: every node that carries a mesh becomes a placed piece. Three conventions go beyond that. They are read from **node names**, so they work from Blender with nothing but renaming. A file that uses none of them cooks exactly as before.

A **piece** is a mesh node that stays placed, named by the art side (`SM_Foo`). Sockets and collision belong to a piece, and LODs are versions of one.

## Sockets: `SOCKET_<name>`

An **empty** node named `SOCKET_<name>` is kept as a named transform instead of being dropped.

- Parent it to the piece's mesh node. The socket is then stored relative to that piece (position and rotation, scale is ignored), so it follows the piece wherever the piece is placed.
- An empty at the scene root belongs to no piece and is stored in file space.
- `<name>` is whatever follows the prefix: `SOCKET_hand_r` is the socket `hand_r`. No whitespace. Names are case sensitive and unique per piece; the prefix must be exactly `SOCKET_` in capitals.
- The node must be an empty. A `SOCKET_` node that carries a mesh stays an ordinary piece.

Engine side (`piece_data.h`, read with `readBundle(path, bundle, pieces)` or got from `cookGltf(..., pieces, ...)`): `findSocket(pieces, "SM_Foo", "hand_r")` returns the socket (`position`, `rotation` as a quaternion) or `nullptr`. A file-level socket is found by any piece name when the piece has none of that name.

## LODs: `<piece>_LOD0`, `_LOD1`, `_LOD2`

Give the meshes of one piece the suffix `_LOD0`, `_LOD1`, `_LOD2` (`SM_Foo_LOD0`, `SM_Foo_LOD1`, ...). The cooker builds one LOD group named `SM_Foo`.

- Levels start at `_LOD0` and follow without gaps; a group has at least two levels and at most three.
- `_LOD0` is the node that is placed (it keeps its full name). The other levels are cooked as meshes and are not placed; the renderer swaps to them by distance.
- Each level has a switch distance, stored in the bundle. It is measured from the bounding radius `r` of `_LOD0` (at least 0.5 m): `_LOD1` takes over at `16 r`, `_LOD2` at `40 r`. A 2 m wide piece switches at 16 m and 40 m.
- Sockets and collision of a piece are named after the piece without the suffix (`SM_Foo`), whichever LOD node they hang from.
- A malformed group (no `_LOD0`, a gap, a second mesh for one level, `_LOD3` or higher) is reported and its meshes stay ordinary placed pieces, so nothing disappears.

Engine side: `findLodGroup(pieces, "SM_Foo")` then `lodMeshAt(group, distance)` gives the mesh index to draw.

## Collision: `UBX_<piece>` and `UCX_<piece>`

A mesh node named `UBX_<piece>` (box) or `UCX_<piece>` (convex) is a collision shape for `<piece>`. It is **not rendered**: it is not cooked as a mesh and is not placed.

- `UBX_`: the cooker stores the axis-aligned box of its vertices in the piece's space (`boundsMin`, `boundsMax`). Model it as a box.
- `UCX_`: the vertices are stored as the hull candidates (`points`, piece space) together with their bounds. The cooker does not build the hull; the physics side does.
- `<piece>` must be the name of a placed piece in the same file. If it is not, the shape is reported and kept in file space.
- Several shapes may belong to one piece.

Engine side: `pieces.collision` lists the shapes with their `piece`.

## Checking a file before it goes in

```
ichor_cook --validate art/settlement_kit/SM_Foo.glb [more files ...]
```

For each file this prints how many sockets, LOD groups and collision shapes it holds, one line for each, and one `issue` line for every name that breaks the rules above (empty socket name, `socket_` in lower case, duplicate socket, `_LOD5`, a lone `_LOD1`, `UCX_` without a mesh, a collision shape for a piece that is not there). It reads the file and cooks nothing, and exits 1 only when a file cannot be read. The same issues are logged as warnings by a normal cook.

## What a bundle looks like

A bundle without any of these nodes carries no extra block. One that has them carries one after the nodes, tagged `IPC1`: sockets, LOD groups, collision shapes.

The format version is 4. Version 3 (uncompressed textures) bundles are refused with a message to cook again; nothing converts them.

## Textures: size caps and compression

The cooker keeps no texture at full source size. Every embedded image goes through three steps, in this order:

1. **Size cap.** `art/texture_budget.json` maps folder prefixes to the largest edge a texture may keep: `art/props/` 1024, `art/props/terrain/` 512, `art/exile/` and the boss, Bull-Warden and Hill Cyclops folders 2048, and 1024 for any path no prefix matches. The longest matching prefix wins. A bigger texture is box-filtered down by halves until its larger edge fits; a smaller one is never upscaled. The cooker finds the file in the `art/` folder above the glTF, so the app and `ichor_cook` agree. Change a cap by editing that one file; a texture that must stay sharp belongs under a prefix with a higher number.
2. **Mip chain.** The full chain is built from the capped size, averaging colour in linear light.
3. **Block compression** (vendored `bc7enc` and `rgbcx`, see `CREDITS.md`). The format follows how a material uses the image:

| Use | Format |
|---|---|
| base colour | BC7 sRGB |
| normal map | BC5 (red and green; the shader rebuilds blue) |
| metallic and roughness, occlusion, anything else | BC7 unorm |
| the same, when the source PNG has one grey channel | BC4 (read as grey) |

An image used as colour and as data keeps the colour format; one used as normal and as data keeps the data format. Sizes that are not a multiple of four are padded to whole blocks by repeating the edge.

A device without `textureCompressionBC` gets the RGBA8 decode of each texture at upload time and renders the same scene at the old memory cost. Set `ICHOR_NO_BC=1` to force that path on any machine.

