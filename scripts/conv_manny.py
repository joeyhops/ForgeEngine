"""
conv_manny.py — FBX → GLB for the full UE5 SK_Manny skeleton, Blender 5.x

Target skeleton: skel_base.glb (90 joints, all 5 spine bones, both neck
bones, metacarpals, and _twist_02 bones retained).

Because the target skeleton is the FULL Manny hierarchy, no bone deletion or
world-space rebaking is required. The pipeline is:

  1. Import FBX
  2. Fix armature object scale (UE5 imports land with scale=0.01)
  3. Strip root bone scale keyframes (same UE5/Blender artefact as before)
  4. Strip non-deform bone fcurves (IK targets, virtual helpers)
  5. Push actions onto NLA so the glTF exporter can see them
  6. Export animation-only GLB

Usage:
    blender --background --factory-startup --python scripts/conv_manny.py -- \\
            --in /path/to/fbx/dir --out /path/to/glb/dir [--recursive] [--legacy-fbx] [--debug]
"""

import bpy
import sys
import re
import argparse
import traceback
from pathlib import Path


# ---------------------------------------------------------------------------
# CLI
# ---------------------------------------------------------------------------

def parse_args():
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    p = argparse.ArgumentParser()
    p.add_argument("--in",       dest="indir",    required=True)
    p.add_argument("--out",      dest="outdir",   required=True)
    p.add_argument("--recursive",  action="store_true")
    p.add_argument("--legacy-fbx", action="store_true")
    p.add_argument("--debug",      action="store_true")
    return p.parse_args(argv)


DEBUG = False

def log(*args):  print("  ", *args)
def dbg(*args):
    if DEBUG: print("   DBG:", *args)


# ---------------------------------------------------------------------------
# Scene reset
# ---------------------------------------------------------------------------

def reset_scene():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for col in (
        bpy.data.objects, bpy.data.meshes, bpy.data.armatures,
        bpy.data.actions, bpy.data.materials, bpy.data.images,
        bpy.data.node_groups, bpy.data.collections,
        bpy.data.cameras, bpy.data.lights,
    ):
        for item in list(col):
            try:
                col.remove(item, do_unlink=True)
            except Exception:
                pass


# ---------------------------------------------------------------------------
# FBX import
# ---------------------------------------------------------------------------

def import_fbx(path: str, use_legacy: bool = False):
    common = dict(
        filepath=path,
        use_anim=True,
        anim_offset=0.0,
        ignore_leaf_bones=False,
        use_custom_normals=True,
        use_custom_props=True,
    )
    if use_legacy:
        # Legacy Python importer: preserve UE5 bone orientations
        bpy.ops.import_scene.fbx(
            **common,
            automatic_bone_orientation=False,
            bake_space_transform=False,
            use_prepost_rot=True,
            global_scale=1.0,
        )
    else:
        # Default Blender 5.x ufbx importer
        bpy.ops.wm.fbx_import(
            **common,
            validate_meshes=True,
            import_colors="SRGB",
            global_scale=1.0,
        )


# ---------------------------------------------------------------------------
# FCurve iteration — handles Blender 4.x flat API and 5.x slotted API
# ---------------------------------------------------------------------------

def iter_fcurves(action):
    """Yield every FCurve in an action."""
    try:
        for layer in action.layers:
            for strip in layer.strips:
                for cb in strip.channelbags:  # .channelbags (plural), not .channelbag(slot)
                    yield from cb.fcurves
        return
    except AttributeError:
        pass
    yield from action.fcurves


def remove_fc(action, fc):
    """Remove an FCurve under both Blender APIs."""
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


# ---------------------------------------------------------------------------
# Step 1 — Fix armature object scale
# ---------------------------------------------------------------------------
# UE5 FBX exports bake a cm→m factor onto the armature object (scale=0.01).
# We set it back to unit scale directly. We do NOT use transform_apply here —
# that would rescale edit bone positions, invalidating the fcurve data.
# Setting the object scale property is sufficient for animation-only exports.

def fix_armature_scale():
    for obj in bpy.data.objects:
        if obj.type != "ARMATURE":
            continue
        s = obj.scale[:]
        if abs(s[0] - 1.0) > 0.0001:
            obj.scale = (1.0, 1.0, 1.0)
            log(f"Fixed armature '{obj.name}' scale: {s[0]:.6f} → 1.0")
        else:
            dbg(f"Armature '{obj.name}' scale already 1.0")
        # Reset root pose-bone scale if the importer wrote one
        if "root" in obj.pose.bones:
            obj.pose.bones["root"].scale = (1.0, 1.0, 1.0)


# ---------------------------------------------------------------------------
# Step 2 — Strip root bone scale keyframes
# ---------------------------------------------------------------------------
# Blender's FBX importer writes scale=(0.01,0.01,0.01) keyframes on
# pose.bones["root"] to reconcile UE5's TransformLink factor. Those keyframes
# collapse child bone translations when the animation is evaluated.
# Delete them; fix_armature_scale() above already resets the live value.

def strip_root_scale(bone_name: str = "root"):
    pattern = f'pose.bones["{bone_name}"].scale'
    stripped = 0
    for action in bpy.data.actions:
        to_remove = [fc for fc in iter_fcurves(action) if fc.data_path == pattern]
        for fc in to_remove:
            remove_fc(action, fc)
            stripped += 1
    log(f"strip_root_scale: removed {stripped} fcurve(s) on '{bone_name}'")


# ---------------------------------------------------------------------------
# Step 3 — Strip non-deform bone fcurves
# ---------------------------------------------------------------------------
# Removes fcurves for IK target and virtual helper bones. These are never
# keyframed in UE5 source animations (they're driven by constraints at
# runtime), so this step is usually a no-op — it's a safety net for FBX
# exports that include baked constraint data.
#
# What is KEPT vs conv_ue5.py:
#   spine_04, spine_05 — present in skel_base.glb, must be retained
#   neck_02            — present in skel_base.glb, must be retained
#   *_twist_02         — present in skel_base.glb, must be retained
#   *_metacarpal       — present in skel_base.glb, must be retained
#   root (location/rotation) — NOT in the skin joint list but its translation
#                              track is read by AnimationClip::sampleRootDelta()
#                              for root motion; keep it
#
# What is STRIPPED:
#   ik_*               — IK target bones, no deform contribution
#   center_of_mass     — virtual helper
#   interaction        — virtual helper

_BONE_RX = re.compile(
    r'pose\.bones\["([^"]+)"\]\.(rotation_quaternion|rotation_euler|location|scale)'
)

NON_DEFORM_EXACT  = {"center_of_mass", "interaction"}
NON_DEFORM_PREFIX = ("ik_",)
# Nothing in NON_DEFORM_SUBSTR — twist_02 and metacarpals are kept for full skeleton


def is_non_deform(name: str) -> bool:
    if name in NON_DEFORM_EXACT:
        return True
    if any(name.startswith(p) for p in NON_DEFORM_PREFIX):
        return True
    return False


def strip_nondeform_fcurves():
    stripped = 0
    for action in bpy.data.actions:
        to_remove = []
        for fc in iter_fcurves(action):
            m = _BONE_RX.match(fc.data_path)
            if m and is_non_deform(m.group(1)):
                to_remove.append(fc)
        for fc in to_remove:
            remove_fc(action, fc)
            stripped += 1
    log(f"strip_nondeform: removed {stripped} fcurve(s)")


# ---------------------------------------------------------------------------
# Step 4 — Push actions onto NLA tracks
# ---------------------------------------------------------------------------
# The glTF exporter only exports the active action OR actions on NLA tracks.
# After a multi-take FBX import extra actions are orphaned; stash them all.

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

    pushed = 0
    for act in bpy.data.actions:
        if act in bound:
            continue
        tr = ad.nla_tracks.new()
        tr.name = act.name
        tr.strips.new(act.name, int(act.frame_range[0]), act)
        pushed += 1
        dbg(f"Pushed '{act.name}' onto NLA")
    log(f"push_actions_to_nla: stashed {pushed} action(s)")


# ---------------------------------------------------------------------------
# Step 5 — Export animation-only GLB
# ---------------------------------------------------------------------------

def export_glb(path: str):
    valid = set()
    try:
        valid = {p.identifier for p in bpy.ops.export_scene.gltf.get_rna_type().properties}
    except Exception:
        pass

    def add(kw, k, v):
        if k in valid:
            kw[k] = v

    kw = {"filepath": path, "export_format": "GLB"}

    # Transform
    add(kw, "export_yup",            True)
    add(kw, "export_apply",          False)
    add(kw, "use_selection",         False)
    add(kw, "use_visible",           False)
    add(kw, "use_renderable",        False)
    add(kw, "export_extras",         False)

    # Geometry — animation file carries no mesh data
    add(kw, "export_texcoords",      False)
    add(kw, "export_normals",        False)
    add(kw, "export_skins",          False)
    add(kw, "export_all_influences", False)
    add(kw, "export_morph",          False)
    add(kw, "export_morph_normal",   False)

    # Bones
    # export_def_bones=False so the root bone's track is always included,
    # which is required for AnimationClip::sampleRootDelta() to find it.
    # Blender may not mark root as a deformation bone (it has no vertex
    # weights), so True would silently drop the root track.
    add(kw, "export_def_bones",      False)

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


# ---------------------------------------------------------------------------
# Pipeline
# ---------------------------------------------------------------------------

def get_armature():
    for obj in bpy.data.objects:
        if obj.type == "ARMATURE":
            return obj
    return None


def process_file(fbx_path: Path, glb_path: Path, use_legacy: bool):
    print(f"\n{'─' * 60}")
    print(f"  FBX : {fbx_path.name}")
    print(f"  GLB : {glb_path}")

    reset_scene()

    log("[1/5] Importing FBX...")
    import_fbx(str(fbx_path), use_legacy=use_legacy)

    arm = get_armature()
    if arm is None:
        raise RuntimeError("No armature found after import")
    log(f"Armature: '{arm.name}'  scale={tuple(round(v, 6) for v in arm.scale)}"
        f"  bones={len(arm.pose.bones)}")

#    log("[2/5] Fixing armature scale...")
#   fix_armature_scale()

    log("[3/5] Stripping root bone scale keyframes...")
    strip_root_scale("root")

    log("[4/5] Stripping IK/helper bone fcurves...")
    strip_nondeform_fcurves()

    log("[5/5] Exporting GLB...")
    push_actions_to_nla()
    glb_path.parent.mkdir(parents=True, exist_ok=True)
    export_glb(str(glb_path))
    log("Done")


# ---------------------------------------------------------------------------
# Entry point
# ---------------------------------------------------------------------------

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
        list(in_dir.glob(pattern)) + list(in_dir.glob(pattern.replace(".fbx", ".FBX")))
        if p.suffix.upper() == ".FBX"
    })

    if not files:
        print(f"No FBX files found under '{in_dir}'")
        sys.exit(0)

    print(f"Found {len(files)} FBX file(s)  →  {out_dir}")

    failed = []
    for i, fbx in enumerate(files, 1):
        glb = (out_dir / fbx.relative_to(in_dir)).with_suffix(".glb")
        print(f"\n[{i}/{len(files)}]", end="")
        try:
            process_file(fbx, glb, use_legacy=args.legacy_fbx)
        except Exception:
            traceback.print_exc()
            failed.append(str(fbx))

    print(f"\n{'=' * 60}")
    print(f"Complete.  OK={len(files) - len(failed)}  Failed={len(failed)}")
    for f in failed:
        print(f"  FAILED: {f}")
    sys.exit(1 if failed else 0)


if __name__ == "__main__":
    main()
