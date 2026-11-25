"""Offline PA frame planning. Quaternion order: x,y,z,w; units: centimetres."""
import hashlib
import json
import math
import re
from pathlib import Path


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def read_json(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def write_json(path, data):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def load_job(path):
    path = Path(path).resolve(strict=True)
    job = read_json(path)
    if job.get("schema") != 1 or not re.fullmatch(r"[a-z0-9][a-z0-9_-]{0,63}", job.get("job_id", "")):
        raise ValueError("Expected schema=1 and a simple job_id")
    for side in ("source", "target"):
        data = job[side]
        if not Path(data["project_dir"]).is_absolute():
            raise ValueError(side + ": project_dir must be absolute")
        for field in ("pa", "mesh"):
            if not data[field].startswith("/Game/") or "." in data[field] or ":" in data[field]:
                raise ValueError(side + ": use /Game/... package paths for " + field)
        if data.get("pca") and not data["pca"].startswith("/Game/"):
            raise ValueError(side + ": invalid PCA path")
    output = Path(job["target"]["project_dir"]).resolve() / "Saved/Codex/PhysicsMigration" / job["job_id"]
    return job, output


def add(a, b):
    return [x + y for x, y in zip(a, b)]


def sub(a, b):
    return [x - y for x, y in zip(a, b)]


def norm(v):
    return math.sqrt(sum(x * x for x in v))


def qmul(a, b):
    x, y, z, w = a
    X, Y, Z, W = b
    return [w*X+x*W+y*Z-z*Y, w*Y-x*Z+y*W+z*X,
            w*Z+x*Y-y*X+z*W, w*W-x*X-y*Y-z*Z]


def qnormal(q):
    if len(q) != 4 or not all(math.isfinite(v) for v in q) or norm(q) < 1e-12:
        raise ValueError("Invalid quaternion")
    return [x / norm(q) for x in q]


def qinv(q):
    q = qnormal(q)
    return [-q[0], -q[1], -q[2], q[3]]


def rotate(q, v):
    return qmul(qmul(qnormal(q), [*v, 0]), qinv(q))[:3]


def parse_transform(text):
    def group(key, axes):
        match = re.search(r"\b" + key + r"=\(([^)]*)\)", text)
        if not match:
            raise ValueError("Missing transform field " + key)
        return [float(re.search(r"\b" + a + r"=([-+0-9.eE]+)", match[1])[1]) for a in axes]
    return {"q": qnormal(group("Rotation", "XYZW")), "p": group("Translation", "XYZ"),
            "scale": group("Scale3D", "XYZ")}


def transform_point(pose, point):
    return add(pose["p"], rotate(pose["q"], point))


def inverse_point(pose, point):
    return rotate(qinv(pose["q"]), sub(point, pose["p"]))


def differences(source, target, path=""):
    """Leaf differences for editable-property JSON, preserving false/null/missing distinctions."""
    if isinstance(source, dict) and isinstance(target, dict):
        rows = []
        for key in sorted(source.keys() | target.keys()):
            field = path + "." + key if path else key
            if key not in source or key not in target:
                rows.append({"field": field, "source": source.get(key), "target": target.get(key),
                             "source_present": key in source, "target_present": key in target})
            else:
                rows.extend(differences(source[key], target[key], field))
        return rows
    if isinstance(source, list) and isinstance(target, list) and len(source) == len(target):
        return [r for i, (a, b) in enumerate(zip(source, target)) for r in differences(a, b, f"{path}[{i}]")]
    same = source == target and not (isinstance(source, bool) != isinstance(target, bool))
    return [] if same else [{"field": path, "source": source, "target": target}]


def comparison(job, source, target):
    bone_map = job.get("bone_map", {})
    target_bodies = {b["bone"]: b for b in target["bodies"]}
    expected_bones = {bone_map.get(b["bone"], b["bone"]) for b in source["bodies"]}
    expected_edges = {(bone_map.get(c["child"], c["child"]), bone_map.get(c["parent"], c["parent"])) for c in source["constraints"]}
    target_edges = {(c["child"], c["parent"]) for c in target["constraints"]}
    result = {"topology": {"source_body_count": len(source["bodies"]), "target_body_count": len(target["bodies"]),
        "source_constraint_count": len(source["constraints"]), "target_constraint_count": len(target["constraints"]),
        "missing_target_bodies": sorted(expected_bones-target_bodies.keys()),
        "extra_target_bodies": sorted(target_bodies.keys()-expected_bones),
        "missing_target_constraints": sorted(expected_edges-target_edges),
        "extra_target_constraints": sorted(target_edges-expected_edges)}, "bodies": [],
        "physics_settings": differences(source["physics_settings"], target["physics_settings"]),
        "editor_options": differences(source["editor_options"], target["editor_options"]),
        "pca_authored": differences(source.get("pca"), target.get("pca")),
        "solver_settings": differences(source["solver_settings"], target["solver_settings"]),
        "materials": []}
    for body in source["bodies"]:
        target_name = bone_map.get(body["bone"], body["bone"])
        if target_name in target_bodies:
            other = target_bodies[target_name]
            result["bodies"].append({"source_bone": body["bone"], "target_bone": target_name,
                "geometry_equal": body["geometry"] == other["geometry"],
                "editable_fields": differences(body.get("editable_properties", {}), other.get("editable_properties", {})),
                "source_material": body["material"], "target_material": other["material"]})
    for path, props in source["materials"].items():
        destination = job.get("materials", {}).get(path, path)
        result["materials"].append({"source": path, "target": destination,
                                    "differences": differences(props, target["materials"].get(destination))})
    return result


def build_plan(job, source, target):
    """Plan an injective, unchanged-body-order migration. Missing/merged bodies stop it."""
    bone_map = job.get("bone_map", {})
    required = {b["bone"] for b in source["bodies"]}
    required.update(n for c in source["constraints"] for n in (c["child"], c["parent"]))
    missing = sorted(n for n in required if not bone_map.get(n))
    if missing:
        raise ValueError("Explicit bone_map missing: " + ", ".join(missing))
    mapped = [bone_map[n] for n in required]
    if len(set(n.casefold() for n in mapped)) != len(mapped):
        raise ValueError("Many-to-one bone mapping needs a separately reviewed topology rebuild")
    for label, snapshot, names in (("source", source, required), ("target", target, mapped)):
        absent = sorted(set(names) - snapshot["pose_world"].keys())
        if absent:
            raise ValueError(label + " reference pose missing: " + ", ".join(absent))
        for name in names:
            if not all(math.isfinite(v) for v in snapshot["pose_world"][name]["p"] + snapshot["pose_world"][name]["scale"]):
                raise ValueError(label + ": non-finite reference transform: " + name)
            if any(abs(s - 1) > 1e-5 for s in snapshot["pose_world"][name]["scale"]):
                raise ValueError(label + ": scaled reference bone requires explicit affine adapter: " + name)
    basis = {n: qnormal(job.get("child_basis", {}).get(n, [0, 0, 0, 1])) for n in required}
    scales = {n: float(job.get("geometry", {}).get("uniform_scale_by_bone", {}).get(n, 1)) for n in required}
    if not all(math.isfinite(v) and v > 0 for v in scales.values()):
        raise ValueError("Geometry scales must be finite positive scalars")
    shape_policy = job.get("geometry", {}).get("policy")
    if shape_policy not in ("copy_source", "rotate_and_uniform_scale"):
        raise ValueError("Choose geometry.policy explicitly")
    if shape_policy == "copy_source" and any(abs(abs(basis[n][3])-1) > 1e-8 or abs(scales[n]-1) > 1e-8 for n in required):
        raise ValueError("Changed local basis/scale requires rotate_and_uniform_scale geometry")
    result = {"schema": 1, "job_id": job["job_id"], "constraints": [], "bodies": [], "warnings": []}
    for body in source["bodies"]:
        n = body["bone"]
        result["bodies"].append({"object": body["object"], "source_bone": n, "target_bone": bone_map[n],
                                 "basis": basis[n], "uniform_scale": scales[n], "material": body["material"]})
    for c in source["constraints"]:
        for key in ("pos1", "pos2", "pri_axis1", "sec_axis1", "pri_axis2", "sec_axis2"):
            if len(c[key]) != 3 or not all(math.isfinite(v) for v in c[key]):
                raise ValueError("Invalid constraint vector: " + c["object"] + "." + key)
        for index in (1, 2):
            x, y = c[f"pri_axis{index}"], c[f"sec_axis{index}"]
            if abs(norm(x)-1) > 0.001 or abs(norm(y)-1) > 0.001 or abs(sum(a*b for a, b in zip(x, y))) > 0.001:
                raise ValueError("Source constraint axes are not orthonormal: " + c["object"])
        sc, sp = c["child"], c["parent"]
        tc, tp = bone_map[sc], bone_map[sp]
        s1, s2 = source["pose_world"][sc], source["pose_world"][sp]
        t1, t2 = target["pose_world"][tc], target["pose_world"][tp]
        b = basis[sc]
        p1 = [v * scales[sc] for v in rotate(b, c["pos1"])]
        p2 = inverse_point(t2, transform_point(t1, p1))
        delta = qmul(qmul(qmul(qmul(qinv(t2["q"]), t1["q"]), b), qinv(s1["q"])), s2["q"])
        source_error = norm(sub(transform_point(s1, c["pos1"]), transform_point(s2, c["pos2"])))
        if source_error > job.get("source_anchor_tolerance_cm", 0.01):
            result["warnings"].append({"constraint": c["object"], "source_anchor_error_cm": source_error})
        result["constraints"].append({"object": c["object"], "source_child": sc, "source_parent": sp,
            "child": tc, "parent": tp, "pos1": p1, "pos2": p2,
            "pri_axis1": rotate(b, c["pri_axis1"]), "sec_axis1": rotate(b, c["sec_axis1"]),
            "pri_axis2": rotate(delta, c["pri_axis2"]), "sec_axis2": rotate(delta, c["sec_axis2"]),
            "source_anchor_error_cm": source_error,
            "uncorrected_target_anchor_error_cm": norm(sub(transform_point(t1, c["pos1"]), transform_point(t2, c["pos2"])))})
    pairs = source.get("disabled_pairs")
    result["disabled_pairs"] = None if pairs is None else sorted([sorted([bone_map[a], bone_map[b]]) for a, b in pairs])
    result["expected_counts"] = {"bodies": len(source["bodies"]), "constraints": len(source["constraints"]),
        "disabled_pairs": None if pairs is None else len(pairs),
        "enabled_pairs": None if pairs is None else len(source["bodies"])*(len(source["bodies"])-1)//2-len(pairs)}
    return result
