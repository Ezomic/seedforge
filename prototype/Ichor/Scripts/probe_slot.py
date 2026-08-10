# Read-only. Does ABP_Unarmed actually evaluate a montage slot?
#
# PlayAnimMontage fails silently when the AnimGraph has no Slot node - the montage plays,
# reports success, and never reaches the pose. That is the same silent-success failure as
# the input mappings, so it gets checked before anything is built on top of it.

import unreal

ABP = "/Game/Characters/Mannequins/Anims/Unarmed/ABP_Unarmed"


def log(m):
    unreal.log("[ichor-slot] " + str(m))


abp = unreal.EditorAssetLibrary.load_asset(ABP)
if abp is None:
    log("ABORT: could not load " + ABP)
else:
    found = []
    graphs = []

    # AnimBlueprints keep the AnimGraph alongside the ubergraph pages.
    for prop in ("function_graphs", "ubergraph_pages", "delegate_signature_graphs"):
        try:
            graphs.extend(abp.get_editor_property(prop) or [])
        except Exception:
            pass

    log("graphs found: {}".format([g.get_name() for g in graphs]))

    for g in graphs:
        try:
            nodes = g.get_editor_property("nodes") or []
        except Exception:
            continue
        for n in nodes:
            cls = n.get_class().get_name()
            if "Slot" in cls:
                slot_name = "?"
                for p in ("node", "slot_name"):
                    try:
                        v = n.get_editor_property(p)
                        slot_name = str(v.get_editor_property("slot_name")) if p == "node" else str(v)
                        break
                    except Exception:
                        pass
                found.append("{} in {} (slot: {})".format(cls, g.get_name(), slot_name))

    if found:
        log("SLOT NODES: {}".format(found))
    elif not graphs:
        # Absence of evidence. AnimBlueprint graphs are editor-only and are not exposed to
        # Python, so an empty list means this probe cannot see, not that the asset is
        # empty. Reporting it as a finding would be the same silent-success mistake this
        # script exists to catch.
        log("CANNOT TELL - no graphs exposed to Python. Check the .uasset for "
            "AnimGraphNode_Slot, or open the AnimGraph by hand.")
    else:
        log("NO SLOT NODE - montages will play silently and never reach the pose")
log("done")
