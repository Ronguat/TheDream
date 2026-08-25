"""Reports which skeleton every skeleton-bound asset sits on, and folds a pack into the master.

Run through Tools/SkeletonCheck/skeleton-check.sh, which supplies the editor.

Two skeletons are sanctioned. SK_Master holds everything that was consolidated into it and is
what the character mesh runs on; SwordShield's SK_Mannequin holds the vendor library, reached
by one compatible entry on the master. Drift is a third skeleton appearing -- which is what a
pack migrated from AnimLibrary does, silently.
"""
import unreal
from collections import Counter, deque

MASTER      = "/Game/TheDream/Combat/Characters/SK_Master.SK_Master"
SWORDSHIELD = ("/Game/GDHBundle/SwordShield/DEMO/Characters_SwordShield/Mannequins/Meshes"
               "/SK_Mannequin.SK_Mannequin")
SANCTIONED  = (MASTER, SWORDSHIELD)
SEEDS       = ["/Game/TheDream/Combat/Characters/BP_PlayerCharacter",
               "/Game/TheDream/Combat/Characters/BP_TrainingDummy"]

_ar = unreal.AssetRegistryHelpers.get_asset_registry()
_opts = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,
                                              include_hard_package_references=True)


def _skeleton_of(asset_data):
    tag = asset_data.get_tag_value("Skeleton") or asset_data.get_tag_value("TargetSkeleton")
    if not tag:
        return None
    tag = str(tag)
    return tag.split("'")[1] if "'" in tag else tag


def _played_closure():
    seen, queue = set(), deque(SEEDS)
    while queue:
        pkg = queue.popleft()
        if pkg in seen:
            continue
        seen.add(pkg)
        for dep in (_ar.get_dependencies(pkg, _opts) or []):
            dep = str(dep)
            if dep.startswith("/Game") and dep not in seen:
                queue.append(dep)
    return seen


def report():
    closure = _played_closure()
    everything, played = Counter(), Counter()
    drift = []
    for asset_data in _ar.get_assets_by_path("/Game", recursive=True):
        skeleton = _skeleton_of(asset_data)
        if not skeleton:
            continue
        everything[skeleton] += 1
        in_closure = str(asset_data.package_name) in closure
        if in_closure:
            played[skeleton] += 1
        if skeleton not in SANCTIONED:
            drift.append((str(asset_data.package_name), skeleton, in_closure))

    print("%6s %6s  skeleton" % ("assets", "played"))
    for skeleton, count in everything.most_common():
        mark = "" if skeleton in SANCTIONED else "  <-- UNSANCTIONED"
        print("%6d %6d  %s%s" % (count, played[skeleton], skeleton, mark))

    if not drift:
        print("\nno drift: every skeleton-bound asset is on the master or on SwordShield's")
        return 0
    print("\ndrift -- %d assets on a skeleton that is neither:" % len(drift))
    for pkg, skeleton, in_closure in sorted(drift):
        print("  %-68s %-24s %s" % (pkg, skeleton.rsplit("/", 1)[-1],
                                    "PLAYED" if in_closure else ""))
    print("\nFold it in with:  ./Tools/SkeletonCheck/skeleton-check.sh --fold <skeleton path>")
    print("Or leave it and add it to the master's compatible list, which is enough to play.")
    return len(drift)


def fold(skeleton_path):
    """Consolidate one skeleton into the master: repoint, resolve, delete the redirector."""
    eal = unreal.EditorAssetLibrary
    master, victim = eal.load_asset(MASTER.split(".")[0]), eal.load_asset(skeleton_path)
    if not victim or victim.get_class().get_name() != "Skeleton":
        print("not a Skeleton: %s" % skeleton_path)
        return 1
    if not eal.consolidate_assets(master, [victim]):
        print("consolidate refused")
        return 1

    packages = []
    for ref in (_ar.get_referencers(skeleton_path, _opts) or []):
        asset = eal.load_asset(str(ref))
        if asset:
            asset.modify()
            packages.append(asset.get_outermost())
    if packages:
        unreal.EditorLoadingAndSavingUtils.save_packages(packages, False)
    print("repointed and re-saved %d referencing packages" % len(packages))

    stub = eal.load_asset(skeleton_path)
    if stub and stub.get_class().get_name() == "ObjectRedirector":
        print("redirector deleted: %s" % eal.delete_asset(skeleton_path))
    else:
        print("redirector left in place -- something still resolves through it")
    return 0
