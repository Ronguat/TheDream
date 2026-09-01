"""Stage 1: screen every migrated SwordShield attack clip on timeline geometry alone.

A tier clip is entered partway and must reach its strike exactly T seconds later, so what it
needs is enough clip BEFORE the strike to enter into, and enough after it to cover recovery.
Writes p2_screen.tsv; blend cost is stage 2, over the survivors only.
"""
import math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
AL = unreal.AnimationLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

# Windows the entry point must span, from the socket's swap to that branch's release.
T_HEAVY, TAIL_HEAVY = 0.250, 0.500
T_CHARGED, TAIL_CHARGED = 0.450, 0.600

rows = []
bone_path_cache = {}

assets = AR.get_assets_by_path("/Game/GDHBundle/SwordShield", recursive=True)
paths = sorted({str(a.package_name) for a in assets
                if "Attack" in str(a.asset_name) and str(a.asset_name).endswith("_IP")
                and "_React" not in str(a.asset_name)})
print("candidates:", len(paths))

for i, pkg in enumerate(paths):
    seq = unreal.load_asset(pkg)
    if not isinstance(seq, unreal.AnimSequence):
        continue
    name = seq.get_name()
    n = AL.get_num_frames(seq)
    length = AL.get_sequence_length(seq)
    if not n or length <= 0:
        continue
    fps = n / length
    skel = seq.get_editor_property("skeleton")
    key = str(skel.get_name()) if skel else "?"
    if key not in bone_path_cache:
        bone_path_cache[key] = [str(b) for b in AL.find_bone_path_to_root(seq, "hand_r")]
    path = bone_path_cache[key]

    pts = []
    for f in range(n + 1):
        xf = unreal.Transform()
        for p in AL.get_bone_poses_for_frame(seq, path, f, False):
            xf = xf * p
        t = xf.translation
        pts.append((t.x, t.y, t.z))
    sp = [math.dist(pts[f], pts[f - 1]) * fps for f in range(1, n + 1)]
    if not sp:
        continue
    peak = max(range(len(sp)), key=lambda k: sp[k]) + 1
    strike = peak / fps
    tail = length - strike
    heavy = "Y" if (strike >= T_HEAVY and tail >= TAIL_HEAVY) else "-"
    charged = "Y" if (strike >= T_CHARGED and tail >= TAIL_CHARGED) else "-"
    rows.append((name, f"{length:.3f}", f"{strike:.3f}", f"{tail:.3f}",
                 f"{sp[peak-1]:.0f}", heavy, charged, pkg))
    if (i + 1) % 40 == 0:
        print("  scanned", i + 1)

with open(os.path.join(HERE, "p2_screen.tsv"), "w") as fh:
    fh.write("Clip\tLen\tStrike\tTail\tPeakCmS\tHeavy\tCharged\tPath\n")
    for r in rows:
        fh.write("\t".join(r) + "\n")

print("scanned %d, heavy-eligible %d, charged-eligible %d"
      % (len(rows), sum(1 for r in rows if r[5] == "Y"), sum(1 for r in rows if r[6] == "Y")))
print("DONE")
