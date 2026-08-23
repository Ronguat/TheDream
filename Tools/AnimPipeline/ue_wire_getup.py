"""Grant GA_GetUpAttack on both character Blueprints' CDOs and save them. Run through
run-in-editor.py; the grant goes live on the next editor restart. The ability's own CDO (input
tag, montage, damage effect, immunity tags) is written through the MCP toolset, which can write
GameplayTag values; Python cannot.
"""
import unreal

GA = "/Game/TheDream/Combat/Abilities/GA_GetUpAttack"
CHARACTERS = ["/Game/TheDream/Combat/Characters/BP_PlayerCharacter", "/Game/TheDream/Combat/Characters/BP_TrainingDummy"]

cls = unreal.load_asset(GA).generated_class()
packages = []
for path in CHARACTERS:
    bp = unreal.load_asset(path)
    c = unreal.get_default_object(bp.generated_class())
    lst = list(c.get_editor_property("default_abilities"))
    if cls not in lst:
        lst.append(cls)
    c.set_editor_property("default_abilities", lst)
    bp.modify()
    packages.append(bp.get_outermost())
    print("GRANT", bp.get_name(), [x.get_name() for x in c.get_editor_property("default_abilities")])
print("SAVED", unreal.EditorLoadingAndSavingUtils.save_packages(packages, False))
print("DONE")
