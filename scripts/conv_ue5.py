"""
fbx_to_glb.py — Batch FBX → GLB, UE5 animations on UE4 skeleton, Blender 5.1.1
 
WHY THE PREVIOUS SCRIPTS FAILED
─────────────────────────────────────────────────────────────────────────────
The attempt to fix the torso collapse by removing spine_04/spine_05 fcurves
was wrong in two ways:
 
  1. strip.channelbag(slot) doesn't exist in Blender 5.x — so all fcurve
     manipulation was silently doing nothing.
 
  2. Even if the fcurves HAD been removed, the bones themselves remained in
     the armature. clavicle_l was still a *child of spine_05* in the GLB
     hierarchy, so its local transforms were still expressed relative to
     spine_05. The consumer applied those to a skeleton where clavicle_l's
     parent is spine_03 — off by two full spine segments of height.
 
THE CORRECT FIX: WORLD-SPACE REBAKE
─────────────────────────────────────────────────────────────────────────────
For every keyed frame, sample each bone's ARMATURE-SPACE matrix (pb.matrix).
Then delete spine_04, spine_05, neck_02 from the armature entirely, making
clavicle_l/r and neck_01 direct children of spine_03. Then re-apply the
saved armature-space matrices via pb.matrix = saved — Blender back-solves
the correct LOCAL transforms relative to the new parent automatically.
The exported GLB then has clavicle_l's animation expressed relative to
spine_03, which matches what the consumer skeleton expects.
 
Usage:
    blender --background --factory-startup --python fbx_to_glb.py -- \\
            --in /path/to/fbx --out /path/to/glb [--recursive] [--legacy-fbx] [--debug]
"""
 
import bpy
import sys
import re
import argparse
import traceback
from pathlib import Path
 
 
# ─────────────────────────────────────────────────────────────────────────────
# CLI
# ─────────────────────────────────────────────────────────────────────────────
 
def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--in",      dest="indir",  required=True)
    p.add_argument("--out",     dest="outdir", required=True)
    p.add_argument("--recursive",  action="store_true")
    p.add_argument("--legacy-fbx", action="store_true")
    p.add_argument("--debug",      action="store_true")
    return p.parse_args(argv)
 
 
DEBUG = False
def log(*a):  print("  ", *a)
def dbg(*a):
    if DEBUG: print("   DBG:", *a)
 
 
# ─────────────────────────────────────────────────────────────────────────────
# Scene reset
# ─────────────────────────────────────────────────────────────────────────────
 
def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for col in (bpy.data.objects, bpy.data.meshes, bpy.data.armatures,
                bpy.data.actions, bpy.data.materials, bpy.data.images,
                bpy.data.node_groups, bpy.data.collections,
                bpy.data.cameras, bpy.data.lights):
        for item in list(col):
            try: col.remove(item, do_unlink=True)
            except Exception: pass
 
 
# ─────────────────────────────────────────────────────────────────────────────
# Import
# ─────────────────────────────────────────────────────────────────────────────
 
def import_fbx(path: str, use_legacy: bool = False):
    common = dict(filepath=path, use_anim=True, anim_offset=0.0,
                  ignore_leaf_bones=False, use_custom_normals=True,
                  use_custom_props=True)
    if use_legacy:
        bpy.ops.import_scene.fbx(**common, automatic_bone_orientation=True,
                                 bake_space_transform=False, use_prepost_rot=True,
                                 global_scale=1.0)
    else:
        bpy.ops.wm.fbx_import(**common, validate_meshes=True,
                              import_colors="SRGB", global_scale=1.0)
 
 
# ─────────────────────────────────────────────────────────────────────────────
# FCurve helpers — Blender 4.x AND 5.x
#
# Blender 5.x API: action.layers → strips → strip.channelbags (plural,
# a direct collection). NOT strip.channelbag(slot) — that method doesn't exist.
# ─────────────────────────────────────────────────────────────────────────────
 
def iter_fcurves(action):
    """Yield every FCurve in an action under both APIs."""
    try:
        for layer in action.layers:
            for strip in layer.strips:
                for cb in strip.channelbags:   # ← .channelbags, NOT .channelbag(slot)
                    yield from cb.fcurves
        return
    except AttributeError:
        pass
    yield from action.fcurves
 
 
def get_writable_fcurves(action):
    """Return the collection to add NEW fcurves to."""
    try:
        return action.layers[0].strips[0].channelbags[0].fcurves
    except (AttributeError, IndexError):
        return action.fcurves
 
 
def remove_fc(action, fc):
    try:
        for layer in action.layers:
            for strip in layer.strips:
                for cb in strip.channelbags:
                    if fc in list(cb.fcurves):
                        cb.fcurves.remove(fc)
                        return
    except AttributeError:
        pass
    try:
        action.fcurves.remove(fc)
    except Exception:
        pass
 
 
# ─────────────────────────────────────────────────────────────────────────────
# Step 1 — Fix armature object scale
# ─────────────────────────────────────────────────────────────────────────────
#
# Blender's FBX importer parks UE's cm→m factor (0.01) on the armature object.
# Set it to unit scale. We do NOT use transform_apply here because that would
# rescale edit bone positions and invalidate the animation fcurves. We just set
# the object scale directly. The armature-space bone matrices (pb.matrix) are
# unaffected by object scale, so the world-space rebake below is safe either way.
 
def fix_armature_scale():
    for obj in bpy.data.objects:
        if obj.type != "ARMATURE":
            continue
        s = obj.scale[:]
        dbg(f"Armature '{obj.name}' scale before fix: {s}")
        if abs(s[0] - 1.0) > 0.0001:
            obj.scale = (1.0, 1.0, 1.0)
            log(f"Fixed armature '{obj.name}' scale: {s[0]:.4f} → 1.0")
        # Also reset the root pose-bone scale if it has one
        if "root" in obj.pose.bones:
            obj.pose.bones["root"].scale = (1.0, 1.0, 1.0)
 
 
# ─────────────────────────────────────────────────────────────────────────────
# Step 2 — World-space rebake retargeting
# ─────────────────────────────────────────────────────────────────────────────
#
# This is the core fix. The problem with the previous approach (just deleting
# fcurves for spine_04/05) is that the bones remained in the armature, so
# clavicle_l was still parented to spine_05 in the GLB. Its LOCAL transform
# was still expressed relative to spine_05, so the consumer applied it in the
# wrong coordinate frame.
#
# Correct approach:
#   1. Sample armature-space matrices (pb.matrix) for every bone at every frame
#   2. Delete the intermediate bones from the armature entirely
#   3. Re-apply saved armature-space matrices via pb.matrix = saved_mat
#      Blender automatically back-solves the correct LOCAL transform relative
#      to the new parent (spine_03 for clavicle_l, etc.)
#   4. Insert keyframes — now in the correct local space for the target skeleton
 
def world_space_retarget(arm_obj, bones_to_remove: list):
    """
    Sample → delete intermediate bones → rebake with correct local space.
    """
    if arm_obj.animation_data is None:
        log("No animation data found on armature, skipping retarget")
        return
 
    bpy.context.view_layer.objects.active = arm_obj
    arm_obj.select_set(True)
 
    # ── Phase 1: Sample armature-space matrices ───────────────────────────
 
    # Find which bones are actually children of the ones we're removing,
    # so we can verify the retarget affects the right bones.
    children_of_removed = {
        pb.name for pb in arm_obj.pose.bones
        if pb.parent and pb.parent.name in bones_to_remove
    }
    dbg(f"Bones to remove: {bones_to_remove}")
    dbg(f"Their direct children (will be reparented): {children_of_removed}")
 
    action_snapshots = {}
 
    for action in bpy.data.actions:
        arm_obj.animation_data.action = action
        bpy.context.view_layer.update()
 
        frames = sorted({
            kp.co[0]
            for fc in iter_fcurves(action)
            for kp in fc.keyframe_points
        })
 
        if not frames:
            dbg(f"Action '{action.name}' has no keyframes, skipping")
            continue
 
        snapshots = {}
        for fr in frames:
            bpy.context.scene.frame_set(int(fr))
            bpy.context.view_layer.update()
            snapshots[fr] = {pb.name: pb.matrix.copy() for pb in arm_obj.pose.bones}
 
        action_snapshots[action] = snapshots
        dbg(f"Sampled '{action.name}': {len(frames)} frames, {len(arm_obj.pose.bones)} bones")
 
    log(f"Phase 1 complete: sampled {len(action_snapshots)} action(s)")
 
    # ── Phase 2: Delete intermediate bones ───────────────────────────────
 
    bpy.ops.object.mode_set(mode='EDIT')
    removed = []
    for bn in bones_to_remove:
        if bn in arm_obj.data.edit_bones:
            arm_obj.data.edit_bones.remove(arm_obj.data.edit_bones[bn])
            removed.append(bn)
    bpy.ops.object.mode_set(mode='POSE')
 
    log(f"Phase 2 complete: deleted {removed}")
    if not removed:
        log("WARNING: no bones were deleted — check bone names match the FBX")
 
    # Verify reparenting worked
    for child in children_of_removed:
        if child in arm_obj.pose.bones:
            pb = arm_obj.pose.bones[child]
            new_parent = pb.parent.name if pb.parent else "(none)"
            dbg(f"  '{child}' new parent: '{new_parent}'")
 
    # ── Phase 3: Rebake animations ────────────────────────────────────────
 
    for action, snapshots in action_snapshots.items():
        if not snapshots:
            continue
 
        # Clear ALL existing fcurves — we'll write fresh ones
        for fc in list(iter_fcurves(action)):
            remove_fc(action, fc)
 
        arm_obj.animation_data.action = action
 
        for fr in sorted(snapshots.keys()):
            bpy.context.scene.frame_set(int(fr))
 
            # Apply saved armature-space matrix to each bone.
            # pb.matrix = target sets the armature-space matrix;
            # Blender back-solves the LOCAL transform relative to the
            # CURRENT parent — so after reparenting, clavicle_l gets
            # local space relative to spine_03 automatically.
            snap = snapshots[fr]
            for pb in arm_obj.pose.bones:
                if pb.name in snap:
                    pb.matrix = snap[pb.name]
 
            # One update pass to let Blender propagate the pose
            bpy.context.view_layer.update()
 
            # Keyframe all bones
            for pb in arm_obj.pose.bones:
                pb.keyframe_insert(data_path='location',            frame=fr)
                pb.keyframe_insert(data_path='rotation_quaternion', frame=fr)
 
        log(f"Phase 3: rebaked '{action.name}' "
            f"({len(snapshots)} frames × {len(arm_obj.pose.bones)} bones)")
 
 
# ─────────────────────────────────────────────────────────────────────────────
# Step 3 — Push actions onto NLA so glTF exporter sees them
# ─────────────────────────────────────────────────────────────────────────────
 
def push_actions_to_nla():
    arms = [o for o in bpy.data.objects if o.type == "ARMATURE"]
    if not arms:
        return
    tgt = arms[0]
    if tgt.animation_data is None:
        tgt.animation_data_create()
    ad = tgt.animation_data
 
    bound = {s.action for t in ad.nla_tracks for s in t.strips if s.action}
    if ad.action:
        bound.add(ad.action)
 
    for act in bpy.data.actions:
        if act in bound:
            continue
        tr = ad.nla_tracks.new()
        tr.name = act.name
        tr.strips.new(act.name, int(act.frame_range[0]), act)
        dbg(f"Pushed '{act.name}' onto NLA")
 
 
# ─────────────────────────────────────────────────────────────────────────────
# Step 4 — Export GLB
# ─────────────────────────────────────────────────────────────────────────────
 
def export_glb(path: str):
    valid = set()
    try:
        valid = {p.identifier
                 for p in bpy.ops.export_scene.gltf.get_rna_type().properties}
    except Exception:
        pass
 
    def add(kw, k, v):
        if k in valid: kw[k] = v
 
    kw = {"filepath": path, "export_format": "GLB"}
 
    # Geometry
    add(kw, "export_yup",              True)
    add(kw, "export_apply",            False)
    add(kw, "use_selection",           False)
    add(kw, "use_visible",             False)
    add(kw, "use_renderable",          False)
    add(kw, "export_extras",           False)
    add(kw, "export_texcoords",        False)
    add(kw, "export_normals",          False)
    add(kw, "export_skins",            False)
    add(kw, "export_all_influences",   False)
    add(kw, "export_morph",            False)
    add(kw, "export_morph_normal",     False)
 
    # Animation
    add(kw, "export_animations",              True)
    add(kw, "export_nla_strips",              True)
    add(kw, "export_frame_range",             False)
    add(kw, "export_force_sampling",          True)
    add(kw, "export_animation_mode",          "ACTIONS")
    add(kw, "export_animation_merge",         "ACTION")
    add(kw, "export_anim_single_armature",    True)
    add(kw, "export_reset_pose_bones",        True)
    add(kw, "export_optimize_animation_size", False)
    add(kw, "optimize_animation_size",        False)
    add(kw, "export_negative_frame",          "SLIDE")
 
    bpy.ops.export_scene.gltf(**kw)
 
 
# ─────────────────────────────────────────────────────────────────────────────
# Pipeline
# ─────────────────────────────────────────────────────────────────────────────
 
# Bones present in UE5 animations but absent from the UE4 target skeleton.
# These are the intermediate bones whose deletion triggers the reparenting
# that requires the world-space rebake.
BONES_TO_REMOVE = [
    "spine_04",
    "spine_05",
    "neck_02",
    # Non-deform/IK bones — safe to strip entirely
    "root",             # virtual root, not in target skeleton
    "center_of_mass",
    "interaction",
    "ik_hand_root",
    "ik_hand_gun",
    "ik_hand_r",
    "ik_hand_l",
    "ik_foot_r",
    "ik_foot_l",
    # _twist_02 variants not in target (target only has _twist_01)
    "thigh_twist_02_l",
    "thigh_twist_02_r",
    "calf_twist_02_l",
    "calf_twist_02_r",
    "lowerarm_twist_02_l",
    "lowerarm_twist_02_r",
    "upperarm_twist_02_l",
    "upperarm_twist_02_r",
    # Metacarpals not in target skeleton
    "index_metacarpal_l",
    "middle_metacarpal_l",
    "ring_metacarpal_l",
    "pinky_metacarpal_l",
    "index_metacarpal_r",
    "middle_metacarpal_r",
    "ring_metacarpal_r",
    "pinky_metacarpal_r",
]
 
 
def get_armature() -> bpy.types.Object | None:
    for obj in bpy.data.objects:
        if obj.type == "ARMATURE":
            return obj
    return None
 
 
def process_file(fbx_path: Path, glb_path: Path, use_legacy: bool):
    print(f"\n{'─' * 60}")
    print(f"  {fbx_path.name}")
 
    reset_scene()
 
    log("[1/4] Importing FBX...")
    import_fbx(str(fbx_path), use_legacy=use_legacy)
 
    arm = get_armature()
    if arm is None:
        raise RuntimeError("No armature found after import")
    log(f"Armature: '{arm.name}'  scale={tuple(round(v,4) for v in arm.scale)}  "
        f"bones={len(arm.pose.bones)}")
 
    log("[2/4] Fixing scale...")
    fix_armature_scale()
 
    log("[3/4] World-space retargeting (sample → delete → rebake)...")
    # Only delete bones that actually exist in this armature
    existing  = {pb.name for pb in arm.pose.bones}
    to_remove = [b for b in BONES_TO_REMOVE if b in existing]
    log(f"Will delete {len(to_remove)} of {len(BONES_TO_REMOVE)} listed bones")
    world_space_retarget(arm, to_remove)
 
    log("[4/4] Exporting GLB...")
    push_actions_to_nla()
    glb_path.parent.mkdir(parents=True, exist_ok=True)
    export_glb(str(glb_path))
    log("✓ Done")
 
 
# ─────────────────────────────────────────────────────────────────────────────
# Entry point
# ─────────────────────────────────────────────────────────────────────────────
 
def main():
    global DEBUG
    args  = parse_args()
    DEBUG = args.debug
 
    in_dir  = Path(args.indir).resolve()
    out_dir = Path(args.outdir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)
 
    pattern = "**/*.fbx" if args.recursive else "*.fbx"
    files = sorted({
        p for p in
        list(in_dir.glob(pattern)) + list(in_dir.glob(pattern.upper()))
        if p.suffix.upper() == ".FBX"
    })
 
    if not files:
        print(f"No FBX files found under '{in_dir}'")
        sys.exit(0)
 
    print(f"Found {len(files)} FBX file(s)  →  {out_dir}")
 
    failed = []
    for i, fbx in enumerate(files, 1):
        glb = (out_dir / fbx.relative_to(in_dir)).with_suffix(".glb")
        print(f"[{i}/{len(files)}]", end="")
        try:
            process_file(fbx, glb, use_legacy=args.legacy_fbx)
        except Exception:
            traceback.print_exc()
            failed.append(str(fbx))
 
    print(f"\n{'=' * 60}")
    print(f"Done.  OK={len(files) - len(failed)}  Failed={len(failed)}")
    for f in failed:
        print(f"  FAILED: {f}")
    sys.exit(1 if failed else 0)
 
 
if __name__ == "__main__":
    main()
