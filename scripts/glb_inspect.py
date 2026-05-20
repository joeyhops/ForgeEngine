"""
glb_inspect.py - Diagnostic for skinned GLB Files

Prints magnitudes the ForgeEngine skinning pipeline cares about:
scene-root scale, mesh bounding box, IBM translations, bone rest TRS
and animation keyframe positions. Allows for quick diagnosis of 
internal unit consistency (i.e. cm-internally-consistent, m-internally-consistent,
mixed)

Usage:
    pip install pygltflib numpy
    python scripts/glb_inspect.py file.glb [file.glb...]
    python scripts/glb_inspect.py file.glb --verbose
    python scripts/glb_inspect.py file.glb --json 
"""

import argparse
import json
import struct
import sys
from pathlib import Path

import numpy as np
from pygltflib import GLTF2



_COMPONENT_DTYPE = {
    5120: np.int8, 5121: np.uint8,
    5122: np.int16, 5123: np.uint16,
    5125: np.uint32, 5126: np.float32
}


# glTF accessor type -> element count
_TYPE_COUNT = {
    "SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4,
    "MAT2": 4, "MAT3": 9, "MAT4": 16
}


def read_accessor(gltf: GLTF2, blob: bytes, idx: int) -> np.ndarray:
    acc = gltf.accessors[idx]
    bv = gltf.bufferViews[acc.bufferView]
    dtype = _COMPONENT_DTYPE[acc.componentType]
    comps = _TYPE_COUNT[acc.type]

    offset = (bv.byteOffset or 0) + (acc.byteOffset or 0)
    stride = bv.byteStride or (np.dtype(dtype).itemsize * comps)

    if stride == np.dtype(dtype).itemsize * comps:
        raw = np.frombuffer(blob, dtype=dtype,
                            count=acc.count * comps, offset=offset)
        arr = raw.reshape(acc.count, comps)
    else:
        arr = np.empty((acc.count, comps), dtype=dtype)
        for i in range(acc.count):
            piece = np.frombuffer(blob, dtype=dtype, count=comps,
                                  offset=offset + i * stride)
            arr[i] = piece

    if acc.type == "MAT4":
        arr = arr.reshape(acc.count, 4, 4).transpose(0, 2, 1)

    return arr


def inspect(path: Path, verbose: bool = False) -> dict:
    gltf = GLTF2().load(str(path))
    blob = gltf.binary_blob() or b""

    rep = {
        "file": str(path.resolve()),
        "size_bytes": path.stat().st_size,
        "generator": gltf.asset.generator if gltf.asset else None,
        "gltf_version": gltf.asset.version if gltf.asset else None,
    }
    rep["scene"] = inspect_scene(gltf)
    rep["skins"] = [inspect_skins(gltf, blob, s_idx)
                    for s_idx in range(len(gltf.skins or []))]
    rep["mesh"] = inspect_first_skinned_mesh(gltf, blob)
    rep["bones"] = inspect_bones(gltf, verbose=verbose)
    rep["animations"] = [inspect_animations(gltf, blob, a_idx)
                         for a_idx in range(len(gltf.animations or []))]
    rep["interpretation"] = interpret(rep)
    return rep


def inspect_scene(gltf: GLTF2) -> dict:
    scene = gltf.scenes[gltf.scene or 0]
    root_idx = scene.nodes[0]
    root = gltf.nodes[root_idx]
    return {
        "num_nodes": len(gltf.nodes or []),
        "num_meshes": len(gltf.meshes or []),
        "num_skins": len(gltf.skins or []),
        "num_animations": len(gltf.animations or []),
        "root_node": {
            "index": root_idx,
            "name": root.name,
            "translation": list(root.translation or [0.0, 0.0, 0.0]),
            "rotation": list(root.rotation or [0.0, 0.0, 0.0, 1.0]),
            "scale": list(root.scale or [1.0, 1.0, 1.0]),
        }
    }

def inspect_skins(gltf: GLTF2, blob: bytes, skin_idx: int) -> dict:
    skin = gltf.skins[skin_idx]
    ibms = read_accessor(gltf, blob, skin.inverseBindMatrices)

    pelvis_local = None
    for local_idx, node_idx in enumerate(skin.joints):
        if gltf.nodes[node_idx].name and \
            "pelvis" in gltf.nodes[node_idx].name.lower():
            pelvis_local = local_idx
            break

    sample_indices = [0]
    if len(skin.joints) > 1:
        sample_indices.append(1)
    if pelvis_local is not None and pelvis_local not in sample_indices:
        sample_indices.append(pelvis_local)

    root_joint_node = gltf.nodes[skin.joints[0]]
    scene = gltf.scenes[gltf.scene or 0]
    root_joint_is_scene_root = (skin.joints[0] == scene.nodes[0])

    samples = []
    for local in sample_indices:
        ibm = ibms[local]
        t = ibm[0:3, 3]
        col_lengths = np.array([
            np.linalg.norm(ibm[0:3, 0]),
            np.linalg.norm(ibm[0:3, 1]),
            np.linalg.norm(ibm[0:3, 2]),
        ])
        samples.append({
            "joint_local_idx": local,
            "joint_name": gltf.nodes[skin.joints[local]].name,
            "ibm_translation": [float(t[0]), float(t[1]), float(t[2])],
            "ibm_t_magnitude": float(np.linalg.norm(t)),
            "ibm_col_lengths": [float(x) for x in col_lengths],
            "ibm_determinant": float(np.linalg.det(ibm[0:3, 0:3])),
        })

    return {
        "num_joints": len(skin.joints),
        "root_joint_name": root_joint_node.name,
        "root_joint_is_scene_root": root_joint_is_scene_root,
        "ibm_samples": samples,
    }


def inspect_first_skinned_mesh(gltf: GLTF2, blob: bytes) -> dict | None:
    for mesh_idx, mesh in enumerate(gltf.meshes or []):
        for prim in mesh.primitives:
            attrs = prim.attributes
            if attrs.JOINTS_0 is None or attrs.WEIGHTS_0 is None:
                continue
            positions = read_accessor(gltf, blob, attrs.POSITION)
            joints = read_accessor(gltf, blob, attrs.JOINTS_0)
            weights = read_accessor(gltf, blob, attrs.WEIGHTS_0)

            bmin = positions.min(axis=0)
            bmax = positions.max(axis=0)
            samples = [{
                "vertex_idx": int(i),
                "joints": [int(x) for x in joints[i]],
                "weights": [float(x) for x in weights[i]],
                "weight_sum": float(weights[i].sum()),
            } for i in range(min(3, len(positions)))]

            return {
                "mesh_idx": mesh_idx,
                "vertex_count": int(len(positions)),
                "bbox_min": [float(x) for x in bmin],
                "bbox_max": [float(x) for x in bmax],
                "bbox_size": [float(x) for x in (bmax - bmin)],
                "height_y": float(bmax[1] - bmin[1]),
                "vertex_samples": samples,
            }
    return None


def inspect_bones(gltf: GLTF2, verbose: bool) -> list:
    if gltf.skins:
        skin = gltf.skins[0]
        joint_node_indices = list(skin.joints)
    else:
        # Skinless file (e.g. animation-only): treat scene root's
        # descendants as the implicit skeleton hierarchy.
        scene = gltf.scenes[gltf.scene or 0]
        joint_node_indices = []
        def walk(node_idx):
            joint_node_indices.append(node_idx)
            for child in (gltf.nodes[node_idx].children or []):
                walk(child)
        for root_idx in scene.nodes:
            walk(root_idx)

    joint_lookup = {n: i for i, n in enumerate(joint_node_indices)}
    parent_of = {i: -1 for i in range(len(joint_node_indices))}
    for local, node_idx in enumerate(joint_node_indices):
        for child_node_idx in (gltf.nodes[node_idx].children or []):
            if child_node_idx in joint_lookup:
                parent_of[joint_lookup[child_node_idx]] = local

    rows = []
    for local, node_idx in enumerate(joint_node_indices):
        node = gltf.nodes[node_idx]
        rows.append({
            "local_idx":   local,
            "name":        node.name,
            "parent_idx":  parent_of[local],
            "translation": list(node.translation or [0, 0, 0]),
            "rotation":    list(node.rotation    or [0, 0, 0, 1]),
            "scale":       list(node.scale       or [1, 1, 1]),
        })

    if not verbose and len(rows) > 10:
        return rows[:5] + [{"__ellipsis__": f"...{len(rows) - 10} bones..."}] + rows[-5:]
    return rows

def inspect_animations(gltf: GLTF2, blob: bytes, anim_idx: int) -> dict:
    anim = gltf.animations[anim_idx]

    duration = 0.0
    for sampler in anim.samplers:
        times = read_accessor(gltf, blob, sampler.input)
        if len(times):
            duration = max(duration, float(times.max()))

    def find_channel(target_name: str, path: str):
        for ch in anim.channels:
            node = gltf.nodes[ch.target.node]
            if node.name and target_name.lower() in node.name.lower() \
                    and ch.target.path == path:
                return ch
        return None

    def channel_samples(ch):
        if ch is None:
            return None
        sampler = anim.samplers[ch.sampler]
        times = read_accessor(gltf, blob, sampler.input).flatten()
        values = read_accessor(gltf, blob, sampler.output)

        def sample_at(t):
            if len(times) == 0:
                return None
            idx = np.searchsorted(times, t)
            idx = min(idx, len(values) - 1)
            return [float(x) for x in values[idx]]
        
        return {
            "num_keys": int(len(times)),
            "at_t_0": sample_at(0.0),
            "at_t_mid": sample_at(duration / 2),
            "at_t_end": sample_at(duration),
        }

    return {
        "name": anim.name,
        "duration_seconds": duration,
        "num_channels": len(anim.channels),
        "pelvis_translation": channel_samples(find_channel("pelvis", "translation")),
        "root_translation": channel_samples(find_channel("root", "translation")),
        "root_scale": channel_samples(find_channel("root", "scale")),
        "limb_rotations": {
            bone: channel_samples(find_channel(bone, "rotation"))
            for bone in ["thigh_r", "thigh_l", "calf_r", "calf_l",
                         "upperarm_r", "upperarm_l", "lowerarm_r", "lowerarm_l"]
        },
        "limb_translations": {
            bone: channel_samples(find_channel(bone, "translation"))
            for bone in ["thigh_r", "thigh_l", "calf_r", "calf_l",
                         "foot_r", "foot_l", "hand_r", "hand_l"]
        },
    }

def interpret(rep: dict) -> dict:
    scene_scale = rep["scene"]["root_node"]["scale"]
    pelvis_t = None
    for s in rep["skins"]:
        for sample in s["ibm_samples"]:
            if "pelvis" in (sample["joint_name"] or "").lower():
                pelvis_t = sample["ibm_t_magnitude"]
                break
        if pelvis_t is not None:
            break
    bbox_height = rep["mesh"]["height_y"] if rep["mesh"] else None

    cm_signals = []
    m_signals = []
    if scene_scale and abs(scene_scale[0] - 0.01) < 1e-4:
        cm_signals.append("scene_scale=0.01")
    elif scene_scale and abs(scene_scale[0] - 1.0) < 1e-4:
        m_signals.append("scene_scale=1.0")
    if pelvis_t is not None:
        if pelvis_t > 10:
            cm_signals.append(f"pelvis_IBM_|t|={pelvis_t:.2f}")
        else:
            m_signals.append(f"pelvis_IBM_|t|={pelvis_t:.2f}")
    if bbox_height is not None:
        if bbox_height > 10:
            cm_signals.append(f"bbox_h={bbox_height:.2f}")
        else:
            m_signals.append(f"bbox_h={bbox_height:.2f}")

    if cm_signals and not m_signals:
        verdict = "CM-INTERNALLY-CONSISTENT"
    elif m_signals and not cm_signals:
        verdict = "M-INTERNALLY-CONSISTENT"
    else:
        verdict = "MIXED/NON-STANDARD"

    return {
        "verdict": verdict,
        "cm_signals": cm_signals,
        "m_signals": m_signals
    }


def print_human(rep: dict) -> None:
    line = "=" * 80
    print(line)
    print(f"FILE: {rep['file']}")
    print(f"size: {rep['size_bytes'] / 1e6:.1f} MB   "
        f"generator: {rep['generator']}     glTF v{rep['gltf_version']}")
    print(line)
    print()

    sc = rep["scene"]
    rn = sc["root_node"]
    print(f"SCENE")
    print(f"  nodes: {sc['num_nodes']} meshes: {sc['num_meshes']}  "
        f"skins: {sc['num_skins']} animations: {sc['num_animations']}")
    print(f"  root node[{rn['index']}] \"{rn['name']}\"")
    print(f"    T: {fmt_vec(rn['translation'])}")
    print(f"    R: {fmt_vec(rn['rotation'])}")
    print(f"    S: {fmt_vec(rn['scale'])}")
    print()

    for s_idx, skin in enumerate(rep['skins']):
        print(f"SKIN {s_idx}")
        print(f"  joints: {skin['num_joints']}    "
            f"root_joint: \"{skin['root_joint_name']}\"    "
            f"root_joint_is_scene_root: "
              f"{'YES' if skin['root_joint_is_scene_root'] else 'NO'}")
        for sample in skin["ibm_samples"]:
            print(f"  IBM[{sample['joint_local_idx']}] \"{sample['joint_name']}\"")
            print(f"    translation: {fmt_vec(sample['ibm_translation'])}  "
                f"|t|: {sample['ibm_t_magnitude']:.3f}")
            print(f"    col_lengths: {fmt_vec(sample['ibm_col_lengths'])}  "
                f"det: {sample['ibm_determinant']:+.3f}")
        print()

    if rep["mesh"]:
        m = rep["mesh"]
        print("MESH")
        print(f"  vertices: {m['vertex_count']}")
        print(f"  bbox:  min {fmt_vec(m['bbox_min'])}")
        print(f"         max {fmt_vec(m['bbox_max'])}")
        print(f"         height (Y): {m['height_y']:.3f}")
        print(f"  first 3 vertex influences:")
        for v in m["vertex_samples"]:
            print(f"    v{v['vertex_idx']}: joints {v['joints']}  "
                f"weights {[f'{w:.2f}' for w in v['weights']]}  "
                f"sum={v['weight_sum']:.3f}")
        print()

    print("BONE REST TRS")
    for row in rep["bones"]:
        if "__ellipsis__" in row:
            print(f"  {row['__ellipsis__']}")
            continue
        print(f"  [{row['local_idx']:3d}] {row['name']:<24} "
            f"parent={row['parent_idx']:3d}  "
              f"T{fmt_vec(row['translation'])}  "
              f"R{fmt_vec(row['rotation'])}  "
              f"S{fmt_vec(row['scale'])}  ")
    print()

    if rep["animations"]:
        print("ANIMATIONS")
        for anim in rep["animations"]:
            print(f"  \"{anim['name']}\"  "
                f"duration: {anim['duration_seconds']:.3f}s  "
                f"channels: {anim['num_channels']}")
            for label, key in [("pelvis.T", "pelvis_translation"),
                               ("root.T", "root_translation"),
                               ("root.S", "root_scale")]:
                s = anim[key]
                if s is None:
                    print(f"    {label}: (no channel)")
                    continue
                print(f"    {label}: keys={s['num_keys']}  "
                    f"t=0: {fmt_vec(s['at_t_0'])}  "
                    f"t=mid: {fmt_vec(s['at_t_mid'])}  "
                    f"t=end: {fmt_vec(s['at_t_end'])}")
            print("  limb rotations (frame 0/mid/end):")
            for bone, s in anim["limb_rotations"].items():
                if s is None:
                    print(f"    {bone}.R: (no channel)")
                    continue
                print(f"    {bone}.R t=0: {fmt_vec(s['at_t_0'])}  "
                    f"t=mid: {fmt_vec(s['at_t_mid'])}  "
                    f"t=end: {fmt_vec(s['at_t_end'])}")
            print("  limb translations (frame 0/mid/end):")
            for bone, s in anim["limb_translations"].items():
                if s is None:
                    print(f"    {bone}.T: (no channel)")
                    continue
                print(f"    {bone}.T t=0: {fmt_vec(s['at_t_0'])}  "
                    f"t=mid: {fmt_vec(s['at_t_mid'])}  "
                    f"t=end: {fmt_vec(s['at_t_end'])}")
            print()
            print()
    else:
        print("ANIMATIONS")
        print("  none")

    interp = rep["interpretation"]
    print("INTERPRETATION")
    print(f"  signals (cm): {', '.join(interp['cm_signals']) or '-'}")
    print(f"  signals (m): {', '.join(interp['m_signals']) or '-'}")
    print(f"  --> {interp['verdict']}")
    print(line)
    print()


def fmt_vec(v) -> str:
    if v is None:
        return "None"
    return "(" + ", ".join(f"{x:7.3f}" for x in v) + ")"

def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("files", nargs="+", type=Path,
                    help="One or more .glb files to inspect")
    ap.add_argument("--verbose", action="store_true",
                    help="Print every bone in the rest-TRS table "
                    "(default: first 5 + last 5)")
    ap.add_argument("--json", action="store_true",
                    help="Emit a machine readable JSON report of "
                    "the human report")
    args = ap.parse_args()

    reports = []
    for path in args.files:
        if not path.exists():
            print(f"!! File not found: {path}", file=sys.stderr)
            continue
        rep = inspect(path, verbose=args.verbose)
        reports.append(rep)
        if not args.json:
            print_human(rep)

    if args.json:
        print(json.dumps(reports, indent=2, default=str))
    return 0


if __name__ == "__main__":
    sys.exit(main())

