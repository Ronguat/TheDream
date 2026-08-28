"""Author the hitstun tell's pacing, then re-derive the knockback carry against it.

Run through run-in-editor.py, editor open and PIE stopped. Writes C_HitstunTellPacing
and C_KnockbackPacing and saves both; wire the first to BP_PlayerCharacter and
BP_TrainingDummy's HitstunTellPacingCurve and the second to GA_Attack's
KnockbackTimeMappingCurve.

    The two are coupled and must be generated together. The tell curve decides when the
    clip's stepping happens in wall time; the carry curve paces the capsule by that same
    stepping. Author one alone and the capsule is paced against a clip that no longer moves
    when it thinks it does -- the same fault the knockdown arc hit.

    STEP_FROM/STEP_TO bound the clip's stepping in its own seconds. Inside them the clip runs
    at STEP_RATE; the absorb before them takes whatever compression is left over.
"""
import unreal, math

CLIP = "/Game/GDHBundle/SwordShield/SwordShieldAnimV3/Animation/RM/AS_SwordSwordAnimV3_Hit_Fw_RM"
DIR = "/Game/TheDream/Combat/Data"
TELL, CARRY_CURVE = "C_HitstunTellPacing", "C_KnockbackPacing"
PORTION, HITSTUN, CARRY = 0.684, 0.55, 0.45
STEP_FROM, STEP_TO, STEP_RATE = 0.45, 0.684, 1.0


def _curve(name, cls, factory):
    path = f"{DIR}/{name}"
    if not unreal.EditorAssetLibrary.does_asset_exist(path):
        unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, DIR, cls, factory())
    return unreal.load_asset(path)


def run():
    AL, les = unreal.AnimationLibrary, unreal.EditorLoadingAndSavingUtils
    seq = unreal.load_asset(CLIP)
    n, L = AL.get_num_frames(seq), AL.get_sequence_length(seq)

    # --- the tell's mapping: stun progress -> clip progress through PORTION ---
    step_len = STEP_TO - STEP_FROM
    step_wall = step_len / STEP_RATE
    absorb_wall = HITSTUN - step_wall
    if absorb_wall <= 0:
        raise RuntimeError("the stepping alone exceeds the stun; truncate PORTION or raise STEP_RATE")
    absorb_rate = STEP_FROM / absorb_wall

    def clip_at_wall(w):
        return w * absorb_rate if w <= absorb_wall else STEP_FROM + (w - absorb_wall) * STEP_RATE

    tell_keys = []
    for i in range(41):
        f = i / 40.0
        tell_keys.append((f, min(clip_at_wall(f * HITSTUN) / PORTION, 1.0)))
    tell_keys[-1] = (1.0, min(clip_at_wall(HITSTUN) / PORTION, 1.0))
    tell = _curve(TELL, unreal.CurveFloat, unreal.CurveFloatFactory)
    unreal.TDCurveTools.set_float_curve_keys(
        tell, [k[0] for k in tell_keys], [k[1] for k in tell_keys], False)

    # --- the carry, paced by the clip's travel under that mapping ---
    pts = []
    for k in range(n + 1):
        t = AL.get_bone_pose_for_time(seq, "root", L * k / n, False).translation
        pts.append((L * k / n, float(t.x), float(t.y)))
    x0, y0 = pts[0][1], pts[0][2]
    cum = [(t, math.hypot(x - x0, y - y0)) for t, x, y in pts]

    def travel(ct):
        for i in range(1, len(cum)):
            if ct <= cum[i][0]:
                (t0, d0), (t1, d1) = cum[i - 1], cum[i]
                return d0 + (d1 - d0) * ((ct - t0) / (t1 - t0)) if t1 > t0 else d1
        return cum[-1][1]

    span = travel(clip_at_wall(CARRY))
    carry_keys = [(i / 32.0, min(travel(clip_at_wall(i / 32.0 * CARRY)) / span, 1.0) if span > 0 else i / 32.0)
                  for i in range(33)]
    carry_keys[-1] = (1.0, 1.0)
    carry = _curve(CARRY_CURVE, unreal.CurveFloat, unreal.CurveFloatFactory)
    unreal.TDCurveTools.set_float_curve_keys(
        carry, [k[0] for k in carry_keys], [k[1] for k in carry_keys], False)

    les.save_packages([p for p in les.get_dirty_content_packages()
                       if TELL in p.get_name() or CARRY_CURVE in p.get_name()], False)
    return {"absorb_rate": round(absorb_rate, 3), "step_rate": STEP_RATE,
            "absorb_wall": round(absorb_wall, 3), "step_wall": round(step_wall, 3),
            "clip_at_carry_end": round(clip_at_wall(CARRY), 3),
            "carry_travel_cm": round(span, 1),
            "tell_monotonic": all(tell_keys[i+1][1] >= tell_keys[i][1] - 1e-6 for i in range(40))}


print(run())
