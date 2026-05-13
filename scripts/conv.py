"""
fbx_to_glb_batch.py — Batch FBX→GLB for Blender 5.1.1
Run: blender -b --factory-startup --python fbx_to_glb_batch.py -- \
        --in /path/fbx_in --out /path/glb_out --recursive
"""
import bpy, os, sys, argparse, traceback
from pathlib import Path

def parse_args():
    argv = sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--in",  dest="indir",  required=True)
    p.add_argument("--out", dest="outdir", required=True)
    p.add_argument("--recursive",  action="store_true")
    p.add_argument("--legacy-fbx", action="store_true",
                   help="Use legacy Python FBX importer (import_scene.fbx)")
    return p.parse_args(argv)

def reset_scene():
    """Full wipe — clears orphans so animations don't leak between files."""
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for coll in (bpy.data.objects, bpy.data.meshes, bpy.data.armatures,
                 bpy.data.actions, bpy.data.materials, bpy.data.images,
                 bpy.data.node_groups, bpy.data.collections,
                 bpy.data.cameras, bpy.data.lights):
        for item in list(coll):
            try: coll.remove(item, do_unlink=True)
            except Exception: pass

def import_fbx(path, use_legacy=False):
    if use_legacy:
        bpy.ops.import_scene.fbx(
            filepath=path, use_anim=True, anim_offset=1.0,
            ignore_leaf_bones=False, automatic_bone_orientation=True,
            use_custom_normals=True, use_custom_props=True,
            use_image_search=True)
    else:
        bpy.ops.wm.fbx_import(            # NEW C++ default in 5.x
            filepath=path, use_anim=True, anim_offset=1.0,
            ignore_leaf_bones=False, use_custom_normals=True,
            use_custom_props=True, validate_meshes=True,
            import_colors='SRGB', global_scale=1.0)

def push_actions_to_nla():
    """glTF exporter only emits Actions that are active OR on an NLA track.
       Multi-take FBX leaves extras orphaned, so stash them all."""
    for obj in bpy.data.objects:
        ad = obj.animation_data
        if not ad: continue
        on_tracks = {s.action for t in ad.nla_tracks for s in t.strips if s.action}
        if ad.action and ad.action not in on_tracks:
            tr = ad.nla_tracks.new(); tr.name = ad.action.name
            tr.strips.new(ad.action.name, int(ad.action.frame_range[0]), ad.action)
    # Bind any still-orphan actions to the first armature.
    arms = [o for o in bpy.data.objects if o.type == 'ARMATURE']
    if not arms: return
    tgt = arms[0]
    if tgt.animation_data is None: tgt.animation_data_create()
    ad = tgt.animation_data
    bound = {s.action for t in ad.nla_tracks for s in t.strips if s.action}
    if ad.action: bound.add(ad.action)
    for act in bpy.data.actions:
        if act in bound: continue
        tr = ad.nla_tracks.new(); tr.name = act.name
        tr.strips.new(act.name, int(act.frame_range[0]), act)

def get_valid_gltf_params():
    """Return the set of property names the installed glTF exporter actually accepts."""
    try:
        props = bpy.ops.export_scene.gltf.get_rna_type().properties
        return {p.identifier for p in props}
    except Exception:
        return set()

def export_glb(path):
    valid = get_valid_gltf_params()

    # Build kwargs from only params confirmed to exist in this Blender build
    kwargs = dict(filepath=path, export_format='GLB')

    def add(key, value):
        if key in valid:
            kwargs[key] = value

    # Core
    add('export_yup',              True)
    add('export_apply',            False)
    add('use_selection',           False)
    add('use_visible',             False)
    add('use_renderable',          False)
    add('export_extras',           False)

    # Mesh / skin
    add('export_texcoords',        False)
    add('export_normals',          False)
    add('export_skins',            False)
    add('export_all_influences',   False)
    add('export_morph',            False)
    add('export_morph_normal',     False)

    # Animation — use only long-standing parameter names
    add('export_animations',       True)
    add('export_force_sampling',   True)   # bakes IK/constraints — critical for FBX
    add('export_nla_strips',       True)   # export NLA-stashed actions
    add('export_frame_range',      False)  # don't clip to playback range
    add('export_def_bones',        False)

    # Newer params — added() silently skips them if not present
    add('export_animation_mode',              'ACTIONS')
    add('export_animation_merge',             'ACTION')  # 4.4+ only, skip if missing
    add('export_optimize_animation_size',     False)
    add('optimize_animation_size',            False)      # older name for same thing
    add('export_anim_single_armature',        True)
    add('export_reset_pose_bones',            True)
    add('export_morph_reset_sk_data',         False)
    add('export_negative_frame',              'SLIDE')

    bpy.ops.export_scene.gltf(**kwargs)

def main():
    a = parse_args()
    in_dir, out_dir = Path(a.indir).resolve(), Path(a.outdir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
    pattern = "**/*.fbx" if a.recursive else "*.fbx"
#    pattern = "**/*.FBX" if a.recursive else "*.FBX"
    files = sorted(p for p in in_dir.glob(pattern) if p.is_file())
    failed = []
    for i, fbx in enumerate(files, 1):
        glb = (out_dir / fbx.relative_to(in_dir)).with_suffix(".glb")
        glb.parent.mkdir(parents=True, exist_ok=True)
        print(f"[{i}/{len(files)}] {fbx.name} -> {glb}")
        try:
            reset_scene()
            import_fbx(str(fbx), use_legacy=a.legacy_fbx)
            push_actions_to_nla()
            export_glb(str(glb))
        except Exception:
            traceback.print_exc(); failed.append(str(fbx))
    print(f"Done. OK {len(files)-len(failed)}  Failed {len(failed)}")
    sys.exit(1 if failed else 0)

if __name__ == "__main__":
    main()
