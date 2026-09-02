"""Do the heavy clips contain a SECOND swing after their strike?

The heavy's recovery is 0.500s, and the clip keeps playing through it. A clip carrying a
follow-up attack would show a second rise-and-fall there -- which would read as wind up, strike,
wind up again, and would be clip content rather than any blend artifact.
"""
import math, unreal

AL = unreal.AnimationLibrary
V2 = "/Game/GDHBundle/SwordShield/SwordShieldAnimV2/Animation/IP/AS_SwordShieldAnimV2_"
V3 = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/IP/AS_SwordSwordAnimV3_"
CLIPS = [("H1 Attack5_Stage2", V2 + "Attack5_Stage2_Complete_IP"),
         ("H2 Attack1_Stage5", V2 + "Attack1_Stage5_Complete_IP"),
         ("H3 Attack4_Stage2", V3 + "Attack4_Stage2_Complete_IP")]

for tag, path in CLIPS:
    seq = unreal.load_asset(path)
    n, L = AL.get_num_frames(seq), AL.get_sequence_length(seq)
    fps = n / L
    bp = [str(x) for x in AL.find_bone_path_to_root(seq, "hand_r")]
    z, sp = [], []
    prev = None
    for fr in range(n + 1):
        xf = unreal.Transform()
        for p in AL.get_bone_poses_for_frame(seq, bp, fr, False):
            xf = xf * p
        t = xf.translation
        z.append(t.z)
        if prev:
            sp.append(math.dist((t.x, t.y, t.z), prev) * fps)
        prev = (t.x, t.y, t.z)
    # count peaks in hand speed above a third of the max -- each is a strike
    mx = max(sp)
    peaks = []
    for i in range(1, len(sp) - 1):
        if sp[i] >= sp[i - 1] and sp[i] > sp[i + 1] and sp[i] > mx * 0.35:
            if not peaks or (i - peaks[-1]) > int(0.25 * fps):
                peaks.append(i)
    print("%-20s len %.3f   hand-speed peaks at: %s"
          % (tag, L, ", ".join("%.3f s (%.0f cm/s)" % ((p + 1) / fps, sp[p]) for p in peaks)))
    print("   %s" % ("SINGLE swing" if len(peaks) == 1
                     else "%d SWINGS -- the clip contains a follow-up" % len(peaks)))
print("\nDONE")
