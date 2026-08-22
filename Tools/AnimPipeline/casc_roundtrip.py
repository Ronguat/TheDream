# A2' (Cascadeur half): import each UE-exported clip onto the loaded UE5_Manny scene, export the
# joints back to FBX, and capture the endpoint frame. Run with casc-run.sh.
import csc, os
EXP = r"C:/Users/rross/Documents/Unreal Projects/TheDream/Saved/AnimPipeline/export"
OUT = r"C:/Users/rross/Documents/Unreal Projects/TheDream/Saved/AnimPipeline/roundtrip"
CLIPS = [("Death_Bw_RM", 26), ("Idle1_IP", 0)]

vs = app.current_scene()
ds = vs.domain_scene()
print("scene", vs.name())
mv = ds.model_viewer()
objs = mv.get_objects()
names = [mv.get_object_name(o) for o in objs]
types = {}
for o in objs:
    types[mv.get_object_type_name(o)] = types.get(mv.get_object_type_name(o), 0) + 1
print("objects", len(objs), "by type", types)
print("sample names", sorted(n for n in names if n and not n.startswith("_"))[:12])

fl = app.get_tools_manager().get_tool("FbxSceneLoader").get_fbx_loader(vs)
st = csc.fbx.FbxSettings()
st.mode = csc.fbx.FbxSettingsMode.Binary
st.up_axis = csc.fbx.FbxSettingsAxis.Z
st.bake_animation = True
fl.set_settings(st)
rtf = app.get_tools_manager().get_tool("RenderToFile")
rp = csc.tools.RenderParameters()
rp.width, rp.height, rp.samples = 960, 540, 1

for tag, frame in CLIPS:
    src = os.path.join(EXP, tag + ".fbx")
    print("IMPORT", tag, "takes", fl.get_takes(csc.Path(src)) if hasattr(fl, "get_takes") else "?")
    fl.import_animation(csc.Path(src))
    b = vs.animation_boundary()
    print("  boundary", b.first_frame, b.last_frame, "layers frames", ds.layers_viewer().frames_count())
    dst = os.path.join(OUT, tag + "_rt.fbx")
    fl.export_joints(dst)
    print("  EXPORT", dst, os.path.getsize(dst) if os.path.exists(dst) else "missing")
    ds.set_current_frame(frame)
    rtf.take_image(vs, rp, os.path.join(OUT, tag + "_f%d.png" % frame))
print("DONE")
