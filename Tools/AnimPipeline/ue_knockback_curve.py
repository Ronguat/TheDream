"""Derive the hit knockback's pacing from the stagger clip's own travel.

Run through run-in-editor.py, editor open and PIE stopped. Creates or rewrites
C_KnockbackPacing and reports the shape; wire it to GA_Attack's
KnockbackTimeMappingCurve.

    The defect it fixes: the carry front-loads and the clip back-loads. At a 0.2s carry
    against a 0.55s hitstun the capsule finished all its travel while the clip had stepped
    16% of its own, so the feet slid during the push and the body staggered afterwards
    standing still. Pacing the carry by the clip's cumulative travel puts them in phase.

    Blocked hits deliberately do not take this curve -- they play a different clip and a
    shorter stun. See UTDMeleeAttackAbility::KnockbackBlockedDurationSeconds.
"""
import unreal, math

CLIP = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/RM/AS_SwordSwordAnimV3_Hit_Fw_RM"
OUT_DIR, OUT_NAME = "/Game/TheDream/Combat/Data", "C_KnockbackPacing"
TELL_PORTION = 0.684      # HitstunTellPortionSeconds -- the span the tell actually plays
HITSTUN      = 0.55       # the stun the tell is fitted to
CARRY        = 0.45       # KnockbackDurationSeconds, which must stay inside HITSTUN


def run():
    AL = unreal.AnimationLibrary
    les = unreal.EditorLoadingAndSavingUtils
    seq = unreal.load_asset(CLIP)
    n, L = AL.get_num_frames(seq), AL.get_sequence_length(seq)

    # Cumulative horizontal travel of the clip's own root, in clip time.
    pts = []
    for k in range(n + 1):
        t = AL.get_bone_pose_for_time(seq, "root", L * k / n, False).translation
        pts.append((L * k / n, float(t.x), float(t.y)))
    x0, y0 = pts[0][1], pts[0][2]
    cum = [(t, math.hypot(x - x0, y - y0)) for t, x, y in pts]

    def travel_at(ct):
        for i in range(1, len(cum)):
            if ct <= cum[i][0]:
                (t0, d0), (t1, d1) = cum[i - 1], cum[i]
                return d0 + (d1 - d0) * ((ct - t0) / (t1 - t0)) if t1 > t0 else d1
        return cum[-1][1]

    # The tell plays TELL_PORTION of clip across HITSTUN, so the carry's window in clip time
    # is however much of that the carry covers.
    rate = TELL_PORTION / HITSTUN
    clip_end = CARRY * rate
    span = travel_at(clip_end)

    keys = []
    for i in range(33):
        f = i / 32.0
        keys.append((f, min(travel_at(f * clip_end) / span, 1.0) if span > 0 else f))
    keys[-1] = (1.0, 1.0)

    path = f"{OUT_DIR}/{OUT_NAME}"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(
            OUT_NAME, OUT_DIR, unreal.CurveFloat, unreal.CurveFloatFactory())
    curve = unreal.load_asset(path)
    unreal.TDCurveTools.set_float_curve_keys(
        curve, [k[0] for k in keys], [k[1] for k in keys], False)
    les.save_packages([p for p in les.get_dirty_content_packages()
                       if OUT_NAME in p.get_name()], False)

    mono = all(keys[i + 1][1] >= keys[i][1] - 1e-6 for i in range(len(keys) - 1))
    return {"clip_window": round(clip_end, 3), "travel_in_window_cm": round(span, 1),
            "monotonic": mono, "ends_at": round(keys[-1][1], 4),
            "profile": [round(keys[i][1], 3) for i in range(0, 33, 4)]}


print(run())
