# D (Cascadeur): import the UE rough onto a fresh UE5_Manny scene, drop the keys inside the two
# blend gaps, set the intervals leading into them to AI interpolation, save the scene.
import csc
GAPS = [(4, 9), (31, 44)]          # frames whose keys go; the key before each gap gets AI
ROUGH = r"C:/Users/rross/Documents/Unreal Projects/TheDream/Saved/AnimPipeline/export/GetUpAttack_Rough.fbx"
SCENE = r"C:/Users/rross/Documents/Unreal Projects/TheDream/Saved/AnimPipeline/GetUpAttack.casc"
dsm = app.get_data_source_manager()
print("reload", dsm.load_scene(r"C:\Users\rross\AppData\Local\Cascadeur\samples\UE5_Manny.casc"))
vs = app.current_scene(); ds = vs.domain_scene()
fl = app.get_tools_manager().get_tool("FbxSceneLoader").get_fbx_loader(vs)
st = csc.fbx.FbxSettings(); st.mode = csc.fbx.FbxSettingsMode.Binary; st.up_axis = csc.fbx.FbxSettingsAxis.Z; st.bake_animation = True
fl.set_settings(st)
fl.import_animation(csc.Path(ROUGH))
lv = ds.layers_viewer(); ids = list(lv.all_layer_ids())
print("frames", lv.frames_count(), "layers", len(ids))
def mod(model, update, sc, session):
    ed = model.layers_editor()
    for lid in ids:
        for a, b in GAPS:
            for f in range(a, b + 1):
                ed.unset_section(f, lid)
            def to_ai(section):
                section.interval.interpolation = csc.layers.layer.Interpolation.AI
            ed.change_section(a - 1, lid, to_ai)
print("gaps opened:", ds.modify_with_session("open gaps", mod))
L = lv.layer(ids[1])
print("keys now:", list(L.key_frame_indices()))
for f in (3, 4, 9, 10, 30, 31, 44, 45):
    s = L.section(f) if L.is_key(f) else None
    print(" f%d is_key=%s interp=%s" % (f, L.is_key(f), s.interval.interpolation if s else "-"))
print("saved:", dsm.save_scene_as(SCENE) if hasattr(dsm, "save_scene_as") else "n/a")
