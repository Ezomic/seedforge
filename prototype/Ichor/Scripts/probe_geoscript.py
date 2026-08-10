# Read-only. What can Geometry Script actually do from Python in 5.8?
#
# Needed before authoring weapon geometry: how a dynamic mesh is made, which primitive
# appends exist, and how a dynamic mesh becomes a saved StaticMesh asset.

import unreal


def log(m):
    unreal.log("[ichor-geo] " + str(m))


def names(obj, needles=None):
    out = [n for n in dir(obj) if not n.startswith("_")]
    if needles:
        out = [n for n in out if any(k in n.lower() for k in needles)]
    return sorted(out)


# Which geometry script libraries loaded at all?
libs = [n for n in dir(unreal) if n.startswith("GeometryScript")]
log("GeometryScript libraries: {}".format(sorted(libs)))

prims = getattr(unreal, "GeometryScript_Primitives", None)
log("Primitives: {}".format("PRESENT" if prims else "ABSENT - plugin not loaded"))
if prims:
    log("  appends: {}".format(names(prims, ["append"])))

mk = getattr(unreal, "GeometryScript_CreateNewAssets", None)
log("CreateNewAssets: {}".format("PRESENT" if mk else "absent"))
if mk:
    log("  fns: {}".format(names(mk, ["static_mesh", "asset"])))

# How is an empty dynamic mesh obtained?
for cls in ("DynamicMesh", "DynamicMeshPool"):
    obj = getattr(unreal, cls, None)
    log("unreal.{}: {}".format(cls, "PRESENT" if obj else "absent"))

try:
    dm = unreal.DynamicMesh()
    log("constructed DynamicMesh directly: {}".format(dm))
except Exception as e:
    log("DynamicMesh() failed: {}".format(e))

# Option structs the append calls need
for cls in ("GeometryScriptPrimitiveOptions", "GeometryScriptCreateNewStaticMeshAssetOptions"):
    obj = getattr(unreal, cls, None)
    log("unreal.{}: {}".format(cls, "PRESENT" if obj else "absent"))
    if obj:
        try:
            log("  props: {}".format(names(obj())))
        except Exception as e:
            log("  could not construct: {}".format(e))

log("done")
