"""Speed profile of hand_r across clips, to locate each one's windup apex and strike."""
import json, math, os, unreal

HERE = os.path.dirname(os.path.abspath(__file__))
CLIPS = json.load(open(os.path.join(HERE, "p0_clips.json")))
AL = unreal.AnimationLibrary


def comp_pos(seq, bone, frame):
    path = [str(b) for b in AL.find_bone_path_to_root(seq, bone)]
    xf = unreal.Transform()
    for p in AL.get_bone_poses_for_frame(seq, path, frame, False):
        xf = xf * p
    return xf.translation


for name, path in CLIPS.items():
    seq = unreal.load_asset(path)
    if not seq:
        print(f"{name}: MISSING {path}")
        continue
    n = AL.get_num_frames(seq)
    length = AL.get_sequence_length(seq)
    fps = n / length if length else 30.0
    pts = [comp_pos(seq, "hand_r", f) for f in range(n + 1)]
    sp = [math.dist((pts[f].x, pts[f].y, pts[f].z), (pts[f-1].x, pts[f-1].y, pts[f-1].z)) * fps
          for f in range(1, n + 1)]
    peak = max(range(len(sp)), key=lambda i: sp[i]) + 1
    # The apex: slowest hand moment before the strike, i.e. the cocked pose.
    pre = sp[:peak - 1]
    apex = (min(range(len(pre)), key=lambda i: pre[i]) + 1) if pre else 0
    print(f"\n{name}  len={length:.3f}s frames={n} fps={fps:.0f}")
    print(f"  strike peak: frame {peak} = {peak/fps:.3f}s  ({sp[peak-1]:.0f} cm/s)")
    print(f"  windup apex: frame {apex} = {apex/fps:.3f}s  ({sp[apex-1]:.0f} cm/s)" if pre else "  no pre-peak")
    step = max(1, n // 30)
    print("  cm/s:", " ".join(f"{int(sp[i]):d}" for i in range(0, len(sp), step)))
print("DONE")
