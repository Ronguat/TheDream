"""Write GA_Attack's nine attack cells onto the CDO from a values TSV, then compile the Blueprint.
Run through run-in-editor.py with PIE stopped; it reads ue_seed_cells.json beside this file:

  {"tsv": "Docs/Combat-Values.tsv", "overrides": "<path or empty>", "save": false}

The TSV is Docs/Combat-Values.tsv, in either layout. With cells present (Positions[p].Cells[b].*)
each cell is read straight. In the layout that preceded them (Branches[i] carrying the tier's
tunables, StringSwings[k] carrying hit k+2, TierAnimations the escalation sockets) the seeding is:
a light cell takes what its swing authored, resolving the swing's zero-means-inherit fields to the
branch; a heavy or charged cell takes its branch's values and the socket that position held for
it; H3 and C3 take L3's hitboxes. The overrides JSON replaces any cell field by
"P<pos>B<branch>": {"Montage": path, "EntrySeconds": x, ...}. Prints every cell; save writes the
package, otherwise the trial reverts with the editor.
"""
import json, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
CFG = json.load(open(os.path.join(HERE, "ue_seed_cells.json")))
PROJECT = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
def project_path(p):
    return p if os.path.isabs(p) else os.path.join(PROJECT, p)


TSV = project_path(CFG["tsv"])
OVR = json.load(open(project_path(CFG["overrides"]))) if CFG.get("overrides") else {}
SAVE = bool(CFG.get("save", False))
A = "/Game/TheDream/Combat/Animations/"

vals = {}
for line in open(TSV, encoding="utf-8"):
    if line.startswith("#") or line.startswith("Object\t"):
        continue
    obj, prop, val = line.rstrip("\n").split("\t")
    if obj == "GA_Attack":
        vals[prop] = val


def f(prop):
    return float(vals[prop])


def obj(prop):
    v = vals.get(prop, "None")
    return None if v in ("None", "") else unreal.load_asset(v.split(".")[0])


def kd(prop):
    v = vals[prop]
    return {"NONE": unreal.TDKnockdownType.NONE, "NORMAL": unreal.TDKnockdownType.NORMAL,
            "HARD": unreal.TDKnockdownType.HARD}[v.split(":")[0].strip("<> ").upper()]


def hitboxes(prefix):
    out = []
    i = 0
    while "%s[%d].MaxReachCm" % (prefix, i) in vals:
        p = "%s[%d]." % (prefix, i)
        out.append(unreal.TDAttackHitbox(min_reach_cm=f(p + "MinReachCm"), max_reach_cm=f(p + "MaxReachCm"),
                                         arc_degrees=f(p + "ArcDegrees"), arc_centre_degrees=f(p + "ArcCentreDegrees"),
                                         height_min_cm=f(p + "HeightMinCm"), height_max_cm=f(p + "HeightMaxCm")))
        i += 1
    return out


def branch_field(b, name):
    return "Branches[%d].%s" % (b, name)


def cell_from_cells(pos, b):
    """The mirror already carries the cells: read this one straight."""
    C = lambda n: "Positions[%d].Cells[%d].%s" % (pos, b, n)
    return dict(
        Montage=obj(C("Montage")), EntrySeconds=f(C("EntrySeconds")), ReleaseStartSeconds=f(C("ReleaseStartSeconds")),
        ReleaseSeconds=f(C("ReleaseSeconds")), RecoverySeconds=f(C("RecoverySeconds")), Damage=f(C("Damage")),
        StaminaDamage=f(C("StaminaDamage")), BlockstunSeconds=f(C("BlockstunSeconds")), HitstunSeconds=f(C("HitstunSeconds")),
        KnockdownType=kd(C("KnockdownType")), ParryLockoutSeconds=f(C("ParryLockoutSeconds")), Hitboxes=hitboxes(C("Hitboxes")),
        LungeDistanceCm=f(C("LungeDistanceCm")), LungeDurationSeconds=f(C("LungeDurationSeconds")),
        LungeStrengthCurve=obj(C("LungeStrengthCurve")))


def cell(pos, b):
    """Seed one cell per the rules in the docstring, or read it straight when the mirror has cells."""
    if "Positions[0].Cells[0].Damage" in vals:
        c = cell_from_cells(pos, b)
        for k, v in OVR.get("P%dB%d" % (pos, b), {}).items():
            c[k] = unreal.load_asset(v.split(".")[0]) if k == "Montage" else v
        return c
    B = lambda n: branch_field(b, n)
    c = dict(
        ReleaseSeconds=f(B("ReleaseSeconds")), RecoverySeconds=f(B("RecoverySeconds")), Damage=f(B("Damage")),
        StaminaDamage=f(B("StaminaDamage")), BlockstunSeconds=f(B("BlockstunSeconds")),
        HitstunSeconds=f(B("HitstunSeconds")), KnockdownType=kd(B("KnockdownType")),
        ParryLockoutSeconds=f(B("ParryLockoutSeconds")), Hitboxes=hitboxes(B("Hitboxes")),
        LungeDistanceCm=f(B("LungeDistanceCm")), LungeDurationSeconds=f(B("LungeDurationSeconds")),
        LungeStrengthCurve=obj(B("LungeStrengthCurve")), EntrySeconds=0.0)
    if b == 0:
        if pos == 0:
            c.update(Montage=obj("AttackMontage"), ReleaseStartSeconds=f("ReleaseStartSeconds"))
        else:
            S = lambda n: "StringSwings[%d].%s" % (pos - 1, n)
            c.update(Montage=obj(S("Montage")), ReleaseStartSeconds=f(S("ReleaseStartSeconds")),
                     RecoverySeconds=f(S("RecoverySeconds")), Damage=f(S("Damage")), StaminaDamage=f(S("StaminaDamage")),
                     BlockstunSeconds=f(S("BlockstunSeconds")), HitstunSeconds=f(S("HitstunSeconds")),
                     KnockdownType=kd(S("KnockdownType")), ParryLockoutSeconds=f(S("ParryLockoutSeconds")))
            for name in ("ReleaseSeconds", "LungeDistanceCm", "LungeDurationSeconds"):
                if f(S(name)) > 0.0:
                    c[name] = f(S(name))
            hb = hitboxes(S("Hitboxes"))
            if hb:
                c["Hitboxes"] = hb
    else:
        T = ("TierAnimations[%d]." % (b - 1)) if pos == 0 else ("StringSwings[%d].TierAnimations[%d]." % (pos - 1, b - 1))
        c.update(Montage=obj(T + "Montage"), EntrySeconds=f(T + "EntrySeconds"), ReleaseStartSeconds=f(T + "ReleaseStartSeconds"))
        if pos == 2:
            c["Hitboxes"] = hitboxes("StringSwings[1].Hitboxes")   # L3's 360, seeded once and decoupled after
    for k, v in OVR.get("P%dB%d" % (pos, b), {}).items():
        c[k] = unreal.load_asset(v.split(".")[0]) if k == "Montage" else v
    return c


def make_cell(c):
    return unreal.TDAttackCell(
        montage=c["Montage"], entry_seconds=c["EntrySeconds"], release_start_seconds=c["ReleaseStartSeconds"],
        release_seconds=c["ReleaseSeconds"], recovery_seconds=c["RecoverySeconds"], damage=c["Damage"],
        stamina_damage=c["StaminaDamage"], blockstun_seconds=c["BlockstunSeconds"], hitstun_seconds=c["HitstunSeconds"],
        knockdown_type=c["KnockdownType"], parry_lockout_seconds=c["ParryLockoutSeconds"], hitboxes=c["Hitboxes"],
        lunge_distance_cm=c["LungeDistanceCm"], lunge_duration_seconds=c["LungeDurationSeconds"],
        lunge_strength_curve=c["LungeStrengthCurve"])


BP = unreal.load_asset("/Game/TheDream/Combat/Abilities/GA_Attack")
cdo = unreal.get_default_object(BP.generated_class())
if "Positions[0].Cells[0].Damage" in vals:
    coil = [f("Positions[%d].CoilEndSeconds" % p) for p in range(3)]
else:
    coil = [f("CoilEndSeconds"), f("StringSwings[0].CoilEndSeconds"), f("StringSwings[1].CoilEndSeconds")]
positions = []
for pos in range(3):
    cells = [make_cell(cell(pos, b)) for b in range(3)]
    positions.append(unreal.TDAttackPosition(cells=cells, coil_end_seconds=coil[pos]))
cdo.set_editor_property("Positions", positions)

# The ladder keeps the branch fields that stayed on it; the tunables moved to the cells.
old = list(cdo.get_editor_property("Branches"))
print("ladder branches on the CDO:", len(old))
cdo.modify(); BP.modify()
unreal.BlueprintEditorLibrary.compile_blueprint(BP)

for pi, p in enumerate(cdo.get_editor_property("Positions")):
    for bi, c in enumerate(p.get_editor_property("Cells")):
        m = c.get_editor_property("Montage")
        hb = c.get_editor_property("Hitboxes")
        print("  P%dB%d %-12s entry %.4f rel %.4f | release %.4f recov %.3f dmg %.0f stam %.0f bs %.3f hs %.3f kd %s lock %.4f | lunge %.0f/%.3f | hitbox %s"
              % (pi, bi, m.get_name() if m else "None", c.get_editor_property("EntrySeconds"), c.get_editor_property("ReleaseStartSeconds"),
                 c.get_editor_property("ReleaseSeconds"), c.get_editor_property("RecoverySeconds"), c.get_editor_property("Damage"),
                 c.get_editor_property("StaminaDamage"), c.get_editor_property("BlockstunSeconds"), c.get_editor_property("HitstunSeconds"),
                 str(c.get_editor_property("KnockdownType")).rsplit(".", 1)[-1], c.get_editor_property("ParryLockoutSeconds"),
                 c.get_editor_property("LungeDistanceCm"), c.get_editor_property("LungeDurationSeconds"),
                 ", ".join("%.0fcm/%.0fdeg" % (h.get_editor_property("MaxReachCm"), h.get_editor_property("ArcDegrees")) for h in hb) or "EMPTY"))
    print("  P%d coil end %.4f" % (pi, p.get_editor_property("CoilEndSeconds")))
if SAVE:
    print("SAVED", unreal.EditorLoadingAndSavingUtils.save_packages([BP.get_outermost()], False))
print("DONE")
