# Throwaway: discover the UE 5.8 Enhanced Input Python API surface.
import unreal

def log(m):
    unreal.log("[probe] " + str(m))

imc = unreal.EditorAssetLibrary.load_asset("/Game/Input/IMC_Default")
log("imc = {}".format(imc))

log("--- InputMappingContext editor properties")
for p in ["mappings", "default_key_mappings", "DefaultKeyMappings", "key_mappings"]:
    try:
        v = imc.get_editor_property(p)
        log("  OK  {} -> {} (len {})".format(p, type(v).__name__, len(v) if hasattr(v, "__len__") else "n/a"))
    except Exception as e:
        log("  --  {} : {}".format(p, e))

log("--- dir(InputMappingContext) filtered")
log("  " + ", ".join([d for d in dir(imc) if "map" in d.lower() or "key" in d.lower()]))

log("--- unreal.Key construction")
try:
    k = unreal.Key("SpaceBar")
    log("  unreal.Key('SpaceBar') -> {}  key_name={}".format(k, k.get_editor_property("key_name")))
except Exception as e:
    log("  unreal.Key failed: {}".format(e))

log("--- EnhancedActionKeyMapping fields")
try:
    m = unreal.EnhancedActionKeyMapping()
    log("  " + ", ".join([d for d in dir(m) if not d.startswith("_")][:40]))
except Exception as e:
    log("  EnhancedActionKeyMapping failed: {}".format(e))

log("--- existing mapping sample")
try:
    ms = imc.get_editor_property("mappings")
    for m in list(ms)[:3]:
        a = m.get_editor_property("action")
        k = m.get_editor_property("key")
        log("  action={} key={}".format(a.get_name() if a else None, k.get_editor_property("key_name")))
except Exception as e:
    log("  sample failed: {}".format(e))

log("--- classes with 'PlayerMappable' or 'KeyMapping'")
log("  " + ", ".join([n for n in dir(unreal) if "KeyMapping" in n or "MappableKey" in n][:20]))
