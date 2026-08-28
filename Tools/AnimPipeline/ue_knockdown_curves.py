"""Regenerate the knockdown's carry curves from the animation they have to cooperate with.

Run through run-in-editor.py, with the editor open and PIE stopped. Writes
C_KnockdownArc and C_KnockdownCarry and saves them.

    BODY_APEX is the *body's* lift in cm, not the capsule's. The capsule offset is derived
    from it and runs well above it, because the arc is a correction rather than the motion:
    it is the difference between the path the body should trace and the one AM_Knockdown's
    own pelvis already supplies. Chart the pelvis to check the result, never the actor --
    see Docs/Debug-Instruments.md.

The four values below must match the character's: FALL and SETTLE are KnockdownFallSeconds
and KnockdownCarrySettleSeconds, CLIP_FIT is KnockdownFallClipSeconds. Changing any of them
without re-running this re-times both curves, because both are normalised over FALL+SETTLE.
"""
import unreal, math

FALL, SETTLE, CLIP_FIT = 0.5, 0.16, 0.8
BODY_APEX = 15.0        # cm the body rises; the capsule pays more
PRESS_CM  = 4.0         # pressed into the floor across the skid, so the source's release has
                        # nothing to correct -- without it the capsule snaps in one frame
ARC  = "/Game/TheDream/Combat/Data/C_KnockdownArc"
PACE = "/Game/TheDream/Combat/Data/C_KnockdownCarry"
MONT = "/Game/TheDream/Combat/Animations/AM_Knockdown"


def run():
    AL, les = unreal.AnimationLibrary, unreal.EditorLoadingAndSavingUtils
    arc, pace, m = unreal.load_asset(ARC), unreal.load_asset(PACE), unreal.load_asset(MONT)
    seq = m.get_editor_property('slot_anim_tracks')[0].get_editor_property('anim_track') \
           .get_editor_property('anim_segments')[0].get_editor_property('anim_reference')
    T = FALL + SETTLE

    def comp(bone, fr):
        path = [str(b) for b in AL.find_bone_path_to_root(seq, bone)]
        xf = unreal.Transform()
        for p in AL.get_bone_poses_for_frame(seq, path, fr, False):
            xf = xf * p
        return xf.translation

    n = AL.get_num_frames(seq)
    pel = [comp("pelvis", k).z for k in range(n + 1)]

    def pelvis_at(ct):
        f = max(0.0, min(ct * 30.0, float(n))); i = int(f); r = f - i
        return pel[i] if i >= n else pel[i] * (1 - r) + pel[i + 1] * r

    # Wall time to clip time, read off the baked time-stretch markers rather than assumed.
    marks = list(m.get_editor_property("time_stretch_curve").get_editor_property("markers"))
    t_orig = marks[-1].get_editor_property('time')[0]
    t_min = marks[-1].get_editor_property('time')[1]
    alpha = (t_orig - t_orig / (CLIP_FIT / FALL)) / (t_orig - t_min)
    pairs = [((lambda a: a[0] + (a[1] - a[0]) * alpha)(list(k.get_editor_property('time'))),
              list(k.get_editor_property('time'))[0]) for k in marks]

    def clip_at(w):
        for i in range(1, len(pairs)):
            if w <= pairs[i][0]:
                (w0, c0), (w1, c1) = pairs[i - 1], pairs[i]
                return c0 + (c1 - c0) * ((w - w0) / (w1 - w0)) if w1 > w0 else c1
        return pairs[-1][1]

    stand, lie = pel[0], pel[n]
    landed = lie + 0.05 * (stand - lie)
    t_land = next((w for w in [i / 1000.0 for i in range(1, int(T * 1000))]
                   if pelvis_at(clip_at(w)) <= landed), FALL)

    # Constant speed through the flight handed smoothly to a skid decaying to zero: the
    # share is what makes the two speeds match at the seam rather than stepping.
    P = (2.0 * t_land) / (2.0 * t_land + (T - t_land))

    def pace_at(w):
        if w <= t_land:
            return P * (w / t_land)
        u = (w - t_land) / (T - t_land)
        return P + (1 - P) * (1 - (1 - u) ** 2)

    pk = [(i / 40.0, pace_at(i / 40.0 * T)) for i in range(41)]
    unreal.TDCurveTools.set_float_curve_keys(pace, [k[0] for k in pk], [k[1] for k in pk], False)

    # Ballistic split: rise and fall times go as the square roots of their heights.
    drop = (stand + BODY_APEX) - lie
    t_apex = t_land * (math.sqrt(BODY_APEX) / (math.sqrt(BODY_APEX) + math.sqrt(drop))) \
        if BODY_APEX > 0 else 0.0

    def want(w):
        if w <= 0: return stand
        if w < t_apex:
            u = (t_apex - w) / t_apex
            return stand + BODY_APEX * (1 - u * u)
        if w < t_land:
            u = (w - t_apex) / (t_land - t_apex)
            return (stand + BODY_APEX) - drop * (u * u)
        return lie

    keys, seen = [], []
    for i in range(49):
        w = i / 48.0 * T
        if w >= t_land:
            u = min((w - t_land) / max(0.3 * (T - t_land), 1e-6), 1.0)
            off = -PRESS_CM * u
        else:
            off = want(w) - pelvis_at(clip_at(w))
        p = pace_at(w)
        if not seen or p > seen[-1] + 1e-4:
            seen.append(p); keys.append((p, off))
    if keys[-1][0] < 1.0:
        keys.append((1.0, -PRESS_CM))
    unreal.TDCurveTools.set_vector_curve_keys(
        arc, [k[0] for k in keys], [unreal.Vector(0.0, 0.0, k[1]) for k in keys], False)

    dirty = [p for p in les.get_dirty_content_packages() if "C_Knockdown" in p.get_name()]
    saved = les.save_packages(dirty, False)
    return {"body_apex": BODY_APEX, "capsule_peak": round(max(k[1] for k in keys), 2),
            "t_apex": round(t_apex, 3), "t_land": round(t_land, 3),
            "flight_share": round(P, 3), "saved": saved}


print(run())
