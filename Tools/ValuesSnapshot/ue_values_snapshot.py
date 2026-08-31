"""Generates Docs/Combat-Values.tsv: every designer-facing combat value read off the
live CDOs, one leaf scalar per row. The assets stay authoritative -- the snapshot is
their dated, greppable mirror, so a doc can name a symbol without copying its number
and an offline audit has an authority to diff against.

Run with the editor open, from the project root:
  "C:/Program Files (x86)/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe" \
      Tools/AnimPipeline/run-in-editor.py Tools/ValuesSnapshot/ue_values_snapshot.py

Property names come from parsing Source/TheDream for UPROPERTYs whose Category starts
with "Combat", plus "Movement", which holds the three facing rates from before the
Combat| convention; Transient properties are runtime readouts and excluded. FTD*
struct members are expanded from the same headers whatever their category, struct
members being categorised per struct ("Attack", "Swing", "Hitbox").
A parsed property no Blueprint CDO answers is read off its C++ class CDO instead, so
every parsed name lands somewhere or is reported missing. Blueprint-only variables are
invisible here, which is safe by the standing rule that every tuning value is a C++
UPROPERTY.
"""
import datetime
import re
from pathlib import Path

import unreal

ASSETS = [
    "/Game/TheDream/Combat/Abilities/GA_Attack",
    "/Game/TheDream/Combat/Abilities/GA_Block",
    "/Game/TheDream/Combat/Abilities/GA_Dodge",
    "/Game/TheDream/Combat/Abilities/GA_GetUpAttack",
    "/Game/TheDream/Combat/Abilities/GA_Jump",
    "/Game/TheDream/Combat/Abilities/GA_Parry",
    "/Game/TheDream/Combat/Characters/BP_PlayerCharacter",
    "/Game/TheDream/Combat/Characters/BP_TrainingDummy",
]

RE_UPROP = re.compile(r'^\s*UPROPERTY\s*\((?P<spec>.*)\)\s*$')
RE_DECL = re.compile(r'^\s*[\w:<>,\s\*&]+?[\s\*&](?P<name>\w+)\s*(?::\s*\d+)?\s*(?:=[^;]*)?;')
RE_TYPE = re.compile(r'^\s*(?:class|struct)\s+(?:\w+_API\s+)?(?P<name>[AUF]\w+)')
RE_CAT = re.compile(r'Category\s*=\s*"(?P<cat>[^"]+)"')


def parse_headers(source_root):
    """-> ({class_name: [prop]}, {struct_name: [prop]}) from Combat* categories and
    FTD* struct bodies respectively."""
    class_props, struct_props = {}, {}
    for path in sorted(Path(source_root).rglob("*.h")):
        lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
        current = None
        i = 0
        while i < len(lines):
            m = RE_TYPE.match(lines[i])
            if m:
                current = m.group("name")
            m = RE_UPROP.match(lines[i])
            if m and current:
                cat = RE_CAT.search(m.group("spec"))
                decl = None
                for j in range(i + 1, min(i + 4, len(lines))):
                    d = RE_DECL.match(lines[j])
                    if d:
                        decl = d.group("name")
                        i = j
                        break
                if decl and "Transient" not in m.group("spec"):
                    if current.startswith("F"):
                        struct_props.setdefault(current, []).append(decl)
                    elif cat and (cat.group("cat").startswith("Combat") or cat.group("cat") == "Movement"):
                        class_props.setdefault(current, []).append(decl)
            i += 1
    return class_props, struct_props


def fmt_scalar(v):
    if v is None:
        return "None"
    if isinstance(v, bool):
        return "true" if v else "false"
    if isinstance(v, int):
        return str(v)
    if isinstance(v, float):
        return f"{v:.6g}"
    if isinstance(v, unreal.Object):
        return v.get_path_name()
    if isinstance(v, (unreal.Name, unreal.Text)) or isinstance(v, str):
        return str(v)
    if isinstance(v, unreal.EnumBase):
        return str(v).rsplit(".", 1)[-1]
    if isinstance(v, unreal.GameplayTag):
        try:
            return str(v.get_editor_property("tag_name"))
        except Exception:
            return " ".join(str(v).split())
    if isinstance(v, unreal.GameplayTagContainer):
        try:
            return ",".join(fmt_scalar(t) for t in v.get_editor_property("gameplay_tags")) or "(empty)"
        except Exception:
            pass
    return " ".join(str(v).split())


def emit(rows, label, prefix, v, struct_props):
    if isinstance(v, unreal.Array):
        for i, item in enumerate(v):
            emit(rows, label, f"{prefix}[{i}]", item, struct_props)
        return
    if isinstance(v, unreal.StructBase):
        members = struct_props.get("F" + type(v).__name__)
        if members:
            for m in members:
                try:
                    emit(rows, label, f"{prefix}.{m}", v.get_editor_property(m), struct_props)
                except Exception:
                    rows.append((label, f"{prefix}.{m}", "(unreadable)"))
            return
        if not isinstance(v, (unreal.GameplayTag, unreal.GameplayTagContainer)):
            rows.append((label, prefix, fmt_scalar(v)))
            return
    rows.append((label, prefix, fmt_scalar(v)))


def run():
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
    class_props, struct_props = parse_headers(project / "Source" / "TheDream")
    parsed = sorted({p for props in class_props.values() for p in props})
    print(f"parsed {len(parsed)} Combat* properties across {len(class_props)} classes, "
          f"{sum(len(v) for v in struct_props.values())} struct members across {len(struct_props)} structs")

    rows, emitted = [], set()
    for path in ASSETS:
        bp = unreal.load_asset(path)
        if not bp:
            print(f"MISSING ASSET: {path}")
            continue
        cdo = unreal.get_default_object(bp.generated_class())
        label = path.rsplit("/", 1)[-1]
        for prop in parsed:
            try:
                v = cdo.get_editor_property(prop)
            except Exception:
                continue
            emit(rows, label, prop, v, struct_props)
            emitted.add(prop)

    # A parsed property no Blueprint answers still gets a home: its C++ class CDO.
    for cls, props in sorted(class_props.items()):
        leftovers = [p for p in props if p not in emitted]
        if not leftovers:
            continue
        py_cls = getattr(unreal, cls[1:], None)
        if not py_cls:
            print(f"NO PYTHON CLASS for {cls}: {leftovers}")
            continue
        cdo = unreal.get_default_object(py_cls)
        for prop in leftovers:
            try:
                v = cdo.get_editor_property(prop)
            except Exception:
                print(f"UNREADABLE: {cls}.{prop}")
                continue
            emit(rows, cls, prop, v, struct_props)
            emitted.add(prop)

    rows.sort()
    out = project / "Docs" / "Combat-Values.tsv"
    with out.open("w", encoding="utf-8", newline="\n") as f:
        f.write(f"# Combat values snapshot, regenerated {datetime.date.today()} from the live CDOs."
                " The assets stay authoritative: the asset wins over this file, and this file wins over a number in prose.\n")
        f.write('# Regenerate with the editor open: "C:/Program Files (x86)/UE_5.8/Engine/Binaries/ThirdParty/Python3/Win64/python.exe"'
                " Tools/AnimPipeline/run-in-editor.py Tools/ValuesSnapshot/ue_values_snapshot.py\n")
        f.write("Object\tProperty\tValue\n")
        for r in rows:
            f.write("\t".join(r) + "\n")
    print(f"wrote {len(rows)} rows to {out}")
    for sentinel in [("BP_PlayerCharacter", "KnockdownFallSeconds"), ("BP_PlayerCharacter", "CoilTurnRateDegrees"),
                     ("BP_TrainingDummy", "InputBufferSeconds"), ("GA_Attack", "Branches[2].ReleaseAtSeconds"),
                     ("GA_Attack", "StringSwings[1].ParryLockoutSeconds")]:
        hits = [r[2] for r in rows if (r[0], r[1]) == sentinel]
        print(f"sentinel {sentinel[0]}.{sentinel[1]} = {hits[0] if hits else 'ABSENT'}")


run()
