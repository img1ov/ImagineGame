"""Bootstrap once per editor: py \".../unreal_worker.py\". Stages require a job JSON."""
import json
import math
import os
import re
import shutil
import sys
from pathlib import Path

sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))
import unreal
import toolset_registry
from mapping import (digest, inverse_point, load_job, norm, parse_transform, qmul,
                     read_json, rotate, sub, transform_point, write_json)

PREFIX = "/Game/__CodexPhysicsMigration/"


def require(condition, message):
    if not condition:
        raise RuntimeError(message)


def identity(job, side):
    actual = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
    expected = Path(job[side]["project_dir"]).resolve()
    require(os.path.normcase(str(actual)) == os.path.normcase(str(expected)),
            f"Wrong editor: expected {expected}, got {actual}")
    return actual


def package_path(asset):
    return asset.split(".", 1)[0]


def asset_file(project, package):
    require(package.startswith("/Game/") and ":" not in package and ".." not in package,
            "Only project /Game/ packages are supported: " + package)
    file = (project / "Content" / (package[6:] + ".uasset")).resolve()
    require(file.is_relative_to((project / "Content").resolve()), "Package escapes Content")
    return file


def dirty_names():
    return {str(p.get_name()) for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}


def clean(paths):
    dirty = dirty_names()
    require(not (set(map(package_path, paths)) & dirty), "Unsaved asset changes: " + str(dirty))


def ref(value):
    return package_path(value.get_path_name()) if value else None


def properties(obj):
    schema = json.loads(unreal.ToolsetLibrary.list_struct_properties(obj.get_class()))
    return json.loads(unreal.ToolsetLibrary.get_object_properties(obj, list(schema)))


def export_object(obj, path):
    task = unreal.AssetExportTask()
    task.set_editor_properties(dict(object=obj, filename=str(path), automated=True, prompt=False,
                                    replace_identical=True, exporter=unreal.ObjectExporterT3D()))
    require(unreal.Exporter.run_asset_export_task(task), "Export failed: " + str(path))
    return path.read_text(encoding="utf-8")


def vec(v):
    return [float(v.x), float(v.y), float(v.z)]


def child_object(pa, name):
    obj = unreal.load_object(None, pa.get_path_name() + ":" + name)
    require(obj is not None, "Missing subobject " + name)
    return obj


def parse_pairs(path):
    if not path:
        return None
    text = Path(path).read_text(encoding="utf-8-sig")
    pairs = []
    for block in re.findall(r"Begin Object Class=/Script/PhysicsAssetEditor\.PhysicsAssetCollisionPair\b.*?End Object", text, re.S):
        a = re.search(r'BoneNameA="([^"]+)"', block)
        b = re.search(r'BoneNameB="([^"]+)"', block)
        require(a and b and a[1] != b[1], "Invalid collision-pair clipboard entry")
        pairs.append(tuple(sorted((a[1], b[1]))))
    require(len(pairs) == len(set(pairs)), "Duplicate disabled collision pairs")
    require(pairs or "PhysicsAssetCollisionPair" not in text, "Cannot parse collision pairs")
    return sorted(pairs)


def capture(project, config, output, label, pa_override=None):
    pa = unreal.load_asset(pa_override or config["pa"])
    mesh = unreal.load_asset(config["mesh"])
    require(isinstance(pa, unreal.PhysicsAsset) and isinstance(mesh, unreal.SkeletalMesh), "PA/Mesh type mismatch")
    text = export_object(pa, output / (label + "_pa.t3d"))
    names = sorted(set(re.findall(r'Begin Object Class=/Script/Engine\.(SkeletalBodySetup|PhysicsConstraintTemplate) Name="([^"]+)"', text)))
    result = {"schema": 1, "project_dir": str(project), "pa": package_path(pa.get_path_name()),
              "mesh": config["mesh"], "engine_version": unreal.SystemLibrary.get_engine_version(),
              "dirty_packages": sorted(dirty_names()), "bodies": [], "constraints": [], "materials": {}}
    result["dependencies"] = [str(p) for p in unreal.AssetRegistryHelpers.get_asset_registry().get_dependencies(
        unreal.Name(package_path(pa.get_path_name())), unreal.AssetRegistryDependencyOptions())]
    for cls, name in names:
        obj = child_object(pa, name)
        ci = obj.get_editor_property("DefaultInstance")
        if cls == "SkeletalBodySetup":
            material = obj.get_editor_property("PhysMaterial")
            override = ci.get_editor_property("PhysMaterialOverride")
            row = {"object": name, "bone": str(obj.get_editor_property("BoneName")),
                   "geometry": obj.get_editor_property("AggGeom").export_text(),
                   "default_instance": ci.export_text(), "material": ref(material),
                   "instance_material": ref(override), "editable_properties": properties(obj)}
            result["bodies"].append(row)
            for pm in (material, override):
                if pm:
                    result["materials"][ref(pm)] = properties(pm)
        else:
            row = {"object": name, "child": str(ci.get_editor_property("ConstraintBone1")),
                   "parent": str(ci.get_editor_property("ConstraintBone2")),
                   "default_instance": ci.export_text(), "profile": ci.get_editor_property("ProfileInstance").export_text()}
            for key, native in (("pos1", "Pos1"), ("pos2", "Pos2"), ("pri_axis1", "PriAxis1"),
                                ("sec_axis1", "SecAxis1"), ("pri_axis2", "PriAxis2"), ("sec_axis2", "SecAxis2")):
                row[key] = vec(ci.get_editor_property(native))
            result["constraints"].append(row)
    require(result["bodies"] and len({b["bone"].casefold() for b in result["bodies"]}) == len(result["bodies"]),
            "Empty or duplicate-body snapshot; do not treat a protected-array read failure as zero bodies")
    def array_order(field):
        entries = re.findall(r'^\s+'+field+r'\((\d+)\)="[^\n]*?\x27([^\x27]+)\x27"', text, re.M)
        return [name.rsplit(":", 1)[-1] for _, name in sorted(entries, key=lambda item: int(item[0]))]
    result["body_order"] = array_order("SkeletalBodySetups")
    result["constraint_order"] = array_order("ConstraintSetup")
    require(len(result["body_order"]) == len(result["bodies"]) and len(result["constraint_order"]) == len(result["constraints"]),
            "Unsupported subclass/export format or partial snapshot: serialized array counts differ")
    pose = unreal.AnimPoseExtensions.get_reference_pose(mesh.get_editor_property("skeleton"))
    bones = [str(n) for n in unreal.AnimPoseExtensions.get_bone_names(pose)]
    result["pose_world"] = {n: parse_transform(unreal.AnimPoseExtensions.get_bone_pose(pose, n, unreal.AnimPoseSpaces.WORLD).export_text()) for n in bones}
    result["pose_local"] = {n: parse_transform(unreal.AnimPoseExtensions.get_bone_pose(pose, n, unreal.AnimPoseSpaces.LOCAL).export_text()) for n in bones}
    result["pose_source"] = "Skeleton reference pose; verify it matches the selected Mesh bind pose"
    result["solver_settings"] = pa.get_editor_property("solver_settings").export_text()
    result["solver_type"] = str(pa.get_editor_property("solver_type"))
    result["physics_settings"] = properties(unreal.get_default_object(unreal.PhysicsSettings))
    options = unreal.get_default_object(unreal.load_class(None, "/Script/UnrealEd.PhysicsAssetEditorOptions"))
    result["editor_options"] = properties(options)
    result["disabled_pairs"] = parse_pairs(config.get("clipboard")) if not pa_override else None
    if config.get("clipboard") and not pa_override:
        clipboard = Path(config["clipboard"]).read_text(encoding="utf-8-sig")
        copied_bones = set(re.findall(r'\bBoneName="([^"]+)"', clipboard))
        require(copied_bones == {b["bone"] for b in result["bodies"]}, "Clipboard is not a complete body selection for this PA")
        require(all(a in copied_bones and b in copied_bones for a, b in result["disabled_pairs"]), "Clipboard collision pair references a missing body")
    pca = unreal.load_asset(config["pca"]) if config.get("pca") else None
    result["pca"] = properties(pca) if pca else None
    result["pca_compilation_needed"] = pca.call_method("IsCompilationNeeded") if pca else None
    if not pa_override:
        packages = [config["pa"], *[p for p in result["materials"] if p.startswith("/Game/")]]
        result["binary_files"] = []
        for package in packages:
            file = asset_file(project, package)
            require(file.is_file(), "Missing saved package: " + str(file))
            for ext in (".uasset", ".uexp", ".ubulk"):
                item = file.with_suffix(ext)
                if item.exists():
                    result["binary_files"].append({"package": package, "relative": str(item.relative_to(project)), "sha256": digest(item)})
    write_json(output / (label + ".json"), result)
    return result


def checked_plan(job_file, job, output):
    plan = read_json(output / "plan.json")
    require(plan["job_sha256"] == digest(job_file), "Job changed: make a new plan and review")
    for side, value in plan["snapshot_sha256"].items():
        require(digest(output / (side + ".json")) == value, "Snapshot changed: " + side)
    review = read_json(output / "review.json")
    require(review.get("reviewed_plan_sha256") == digest(output / "plan.json"), "Review does not match this plan")
    for field in ("bone_map", "reference_pose", "geometry", "materials", "pca", "config"):
        require(review.get("decisions", {}).get(field) is True, "Technical review missing: " + field)
    require(not plan["warnings"] or review.get("source_anchor_warnings_reviewed") is True, "Source anchor discrepancies need a decision")
    require(job.get("allow_target_writes") is True, "Job does not authorize target writes")
    return plan


def state_file(output):
    return output / "state.json"


def save_state(output, state):
    write_json(state_file(output), state)


def backup(project, package, output, state):
    path = asset_file(project, package)
    key = str(path.relative_to(project))
    if key not in state["backups"]:
        files = []
        for ext in (".uasset", ".uexp", ".ubulk"):
            item = path.with_suffix(ext)
            if item.exists():
                destination = output / "backup" / item.relative_to(project)
                destination.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(item, destination)
                files.append({"relative": str(item.relative_to(project)), "sha256": digest(item)})
        state["backups"][key] = files
        save_state(output, state)


def unload(package):
    result = unreal.EditorLoadingAndSavingUtils.unload_packages([package])
    success = result[0] if isinstance(result, tuple) else result
    require(bool(success), "Unloading failed; no file replacement attempted: " + str(result))


def set_preview(pa, mesh):
    unreal.get_default_object(unreal.SystemLibrary).call_method("SetSoftObjectPropertyByName",
        args=(pa, unreal.Name("PreviewSkeletalMesh"), mesh))


def prepare(job_file, job, project, output):
    checked_plan(job_file, job, output)
    require(not state_file(output).exists(), "This job already staged assets; inspect state before resuming")
    source, target = read_json(output/"source.json"), read_json(output/"target.json")
    require(source["engine_version"] == target["engine_version"], "Different engine builds need an explicitly reviewed migration path")
    supported_dependencies = {job["source"]["pa"], job["source"]["mesh"], *source["materials"]}
    unsupported = [p for p in source.get("dependencies", []) if p not in supported_dependencies and not p.startswith(("/Engine/", "/Script/"))]
    require(not unsupported, "Source PA has additional dependencies; prepare an explicit dependency adapter: " + str(unsupported))
    clean([job["target"]["pa"], *([job["target"]["pca"]] if job["target"].get("pca") else [])])
    source_packages = {b["package"] for b in source["binary_files"]}
    require(not (source_packages & set(source["dirty_packages"])), "Source disk packages are stale relative to live editor")
    state = {"job_sha256": digest(job_file), "stage": "preparing", "created": [], "backups": {}, "config": None}
    save_state(output, state)
    for package in [job["target"]["pa"], *([job["target"]["pca"]] if job["target"].get("pca") else [])]:
        backup(project, package, output, state)
    source_root = Path(job["source"]["project_dir"]).resolve()
    scan = []
    for item in source["binary_files"]:
        original = (source_root/item["relative"]).resolve()
        destination = (project/item["relative"]).resolve()
        require(original.is_relative_to(source_root/"Content") and destination.is_relative_to(project/"Content"), "Transfer path escapes Content")
        require(digest(original) == item["sha256"], "Source package changed since snapshot")
        if destination.exists():
            require(digest(destination) == item["sha256"], "Conflicting source-path package already exists in target: " + str(destination))
            clean([item["package"]])
        else:
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(original, destination)
            # Record ownership before any later mutation; cleanup removes only created packages.
            if item["package"] not in state["created"]:
                state["created"].append(item["package"])
            save_state(output, state)
        scan.append(str(destination))
    unreal.AssetRegistryHelpers.get_asset_registry().scan_files_synchronous(scan, True)
    material_map = job.get("materials", {})
    for source_path, source_properties in source["materials"].items():
        if source_path.startswith("/Engine/"):
            continue
        require(source_path in material_map, "Specify materials destination for " + source_path)
        destination = material_map[source_path]
        asset_file(project, destination)
        if unreal.EditorAssetLibrary.does_asset_exist(destination):
            require(properties(unreal.load_asset(destination)) == source_properties, "Existing destination material differs: " + destination)
        else:
            obj = unreal.EditorAssetLibrary.duplicate_asset(source_path, destination)
            require(obj is not None, "Material duplication failed")
            state["created"].append(destination)
            save_state(output, state)
            require(unreal.EditorAssetLibrary.save_loaded_asset(obj, False), "Cannot save material")
    candidate = PREFIX + job["job_id"] + "/" + job["target"]["pa"].rsplit("/", 1)[1]
    require(not unreal.EditorAssetLibrary.does_asset_exist(candidate), "Candidate already exists")
    obj = unreal.EditorAssetLibrary.duplicate_asset(job["source"]["pa"], candidate)
    require(obj is not None, "PA duplication failed")
    state["created"].append(candidate)
    state["candidate"] = candidate
    save_state(output, state)
    obj.modify()
    set_preview(obj, unreal.load_asset(job["target"]["mesh"]))
    require(unreal.EditorAssetLibrary.save_loaded_asset(obj, False), "Cannot save candidate")
    state["stage"] = "staged"
    save_state(output, state)


def material_for(job, source_path):
    if source_path is None:
        return None
    return unreal.load_asset(source_path if source_path.startswith("/Engine/") else job["materials"][source_path])


def adapt_geometry(agg, basis, scale):
    # Only analytic primitives are supported; other shapes need a topology/geometry adapter.
    for name in ("ConvexElems", "TaperedCapsuleElems", "LevelSetElems", "SkinnedLevelSetElems", "MLLevelSetElems", "SkinnedTriangleMeshElems"):
        try:
            values = agg.get_editor_property(name)
        except Exception:
            continue  # Field absent in this engine; unsupported serialized shapes are checked in adapt().
        require(not values, "Unsupported geometry for automatic basis/scale conversion: " + name)
    for name in ("SphereElems", "BoxElems", "SphylElems"):
        elements = list(agg.get_editor_property(name))
        for shape in elements:
            shape.set_editor_property("Center", unreal.Vector(*[x*scale for x in rotate(basis, vec(shape.get_editor_property("Center")))]))
            if name != "SphereElems":
                q = shape.get_editor_property("Rotation").quaternion()
                rotation = qmul(basis, [q.x, q.y, q.z, q.w])
                shape.set_editor_property("Rotation", unreal.Quat(*rotation).rotator())
            for dimension in ({"SphereElems": ["Radius"], "SphylElems": ["Radius", "Length"], "BoxElems": ["X", "Y", "Z"]}[name]):
                shape.set_editor_property(dimension, shape.get_editor_property(dimension)*scale)
        agg.set_editor_property(name, elements)
    return agg


def adapt(job_file, job, project, output):
    plan = checked_plan(job_file, job, output)
    state = read_json(state_file(output))
    require(state["stage"] in ("staged", "adapted"), "Candidate is not ready for adaptation")
    pa = unreal.load_asset(state["candidate"])
    source = read_json(output/"source.json")
    bodies = {b["object"]: b for b in source["bodies"]}
    constraints = {c["object"]: c for c in source["constraints"]}
    with unreal.ScopedEditorTransaction("Adapt candidate physics asset from reviewed mapping"):
        pa.modify()
        for row in plan["bodies"]:
            obj = child_object(pa, row["object"])
            original = bodies[row["object"]]
            obj.modify()
            agg = obj.get_editor_property("AggGeom")
            agg.import_text(original["geometry"])
            if job["geometry"]["policy"] == "rotate_and_uniform_scale":
                # Reject serialized unsupported geometry even when a Python field cannot be read.
                for field in ("ConvexElems", "TaperedCapsuleElems", "LevelSetElems", "SkinnedLevelSetElems", "MLLevelSetElems", "SkinnedTriangleMeshElems"):
                    require(not re.search(r'\b'+field+r'=\(\(', original["geometry"]), "Unsupported nonempty shape array: " + field)
                agg = adapt_geometry(agg, row["basis"], row["uniform_scale"])
            obj.set_editor_property("AggGeom", agg)
            if str(obj.get_editor_property("BoneName")) != row["target_bone"]:
                unreal.get_default_object(unreal.SystemLibrary).call_method("SetNamePropertyByName",
                    args=(obj, unreal.Name("BoneName"), unreal.Name(row["target_bone"])))
                require(str(obj.get_editor_property("BoneName")) == row["target_bone"], "Native bone-name setter failed")
            obj.set_editor_property("PhysMaterial", material_for(job, original["material"]))
            instance = obj.get_editor_property("DefaultInstance")
            instance.import_text(original["default_instance"])
            instance.set_editor_property("PhysMaterialOverride", material_for(job, original["instance_material"]))
            obj.set_editor_property("DefaultInstance", instance)
        for row in plan["constraints"]:
            obj = child_object(pa, row["object"])
            obj.modify()
            ci = obj.get_editor_property("DefaultInstance")
            ci.import_text(constraints[row["object"]]["default_instance"])
            ci.set_editor_property("ConstraintBone1", unreal.Name(row["child"]))
            ci.set_editor_property("ConstraintBone2", unreal.Name(row["parent"]))
            for key, native in (("pos1", "Pos1"), ("pos2", "Pos2"), ("pri_axis1", "PriAxis1"),
                                ("sec_axis1", "SecAxis1"), ("pri_axis2", "PriAxis2"), ("sec_axis2", "SecAxis2")):
                ci.set_editor_property(native, unreal.Vector(*row[key]))
            obj.set_editor_property("DefaultInstance", ci)
        require(unreal.EditorAssetLibrary.save_loaded_asset(pa, False), "Candidate save failed")
    # Rebuild transient bone-name lookup caches after renaming body bones.
    package = pa.get_outermost()
    obj = ci = instance = agg = None
    pa = None
    unload(package)
    require(unreal.load_asset(state["candidate"]) is not None, "Candidate reload failed")
    state["stage"] = "adapted"
    save_state(output, state)


def verify(job_file, job, project, output):
    plan = checked_plan(job_file, job, output)
    state = read_json(state_file(output))
    require(state["stage"] in ("adapted", "verified", "committed"), "Candidate has not been adapted")
    path = job["target"]["pa"] if state["stage"] == "committed" else state["candidate"]
    actual = capture(project, job["target"], output, "final" if state["stage"] == "committed" else "candidate", path)
    source = read_json(output/"source.json")
    actual_bodies = {b["object"]: b for b in actual["bodies"]}
    actual_constraints = {c["object"]: c for c in actual["constraints"]}
    require(len(actual_bodies) == len(plan["bodies"]) and len(actual_constraints) == len(plan["constraints"]), "Topology count mismatch")
    require(actual["body_order"] == source["body_order"] and actual["constraint_order"] == source["constraint_order"],
            "Body/constraint array order changed; collision-index preservation is no longer justified")
    source_bodies = {b["object"]: b for b in source["bodies"]}
    for row in plan["bodies"]:
        body = actual_bodies[row["object"]]
        original = source_bodies[row["object"]]
        require(body["bone"] == row["target_bone"], "Body bone mismatch")
        # Restore original material pointers for comparing all remaining BodyInstance fields.
        expected = child_object(unreal.load_asset(path), row["object"]).get_editor_property("DefaultInstance").copy()
        expected.import_text(original["default_instance"])
        expected.set_editor_property("PhysMaterialOverride", material_for(job, original["instance_material"]))
        require(body["default_instance"] == expected.export_text(), "BodyInstance mismatch: " + body["bone"])
        require(body["material"] == ref(material_for(job, original["material"])), "Material mismatch")
        for key, value in original.get("editable_properties", {}).items():
            if key not in ("boneName", "aggGeom", "physMaterial", "defaultInstance"):
                require(body["editable_properties"].get(key) == value, "BodySetup field changed: " + body["bone"] + "." + key)
        if job["geometry"]["policy"] == "copy_source":
            require(body["geometry"] == original["geometry"], "Geometry changed unexpectedly")
        else:
            expected_geom = child_object(unreal.load_asset(path), row["object"]).get_editor_property("AggGeom").copy()
            expected_geom.import_text(original["geometry"])
            expected_geom = adapt_geometry(expected_geom, row["basis"], row["uniform_scale"])
            require(body["geometry"] == expected_geom.export_text(), "Converted geometry readback mismatch")
    max_error = 0
    source_constraints = {c["object"]: c for c in source["constraints"]}
    for row in plan["constraints"]:
        ci = actual_constraints[row["object"]]
        require(ci["child"] == row["child"] and ci["parent"] == row["parent"], "Constraint endpoints differ")
        require(ci["profile"] == source_constraints[row["object"]]["profile"], "Constraint profile changed")
        for key in ("pos1", "pos2", "pri_axis1", "sec_axis1", "pri_axis2", "sec_axis2"):
            require(norm(sub(ci[key], row[key])) < 2e-5, "Frame readback mismatch: " + row["object"] + "." + key)
        error = norm(sub(transform_point(actual["pose_world"][ci["child"]], ci["pos1"]),
                         transform_point(actual["pose_world"][ci["parent"]], ci["pos2"])))
        max_error = max(error, max_error)
    require(max_error < job.get("target_anchor_tolerance_cm", 0.001), "Target anchors still misaligned")
    require(actual["solver_settings"] == source["solver_settings"] and actual["solver_type"] == source["solver_type"], "PA solver mismatch")
    require(not actual["pca_compilation_needed"], "PCA compiled data stale")
    if state["stage"] == "committed" and job["target"].get("pca"):
        expected_pca = remap_pca(source["pca"], job) if job.get("pca_policy") == "copy_source" else read_json(output/"target.json")["pca"]
        expected_pca["physicsAsset"] = {"refPath": unreal.load_asset(path).get_path_name()}
        require(actual["pca"] == expected_pca, "PCA authored-data readback mismatch")
    if state["stage"] == "committed" and job.get("physics_settings_policy") == "copy_source":
        for key in ("bSubstepping", "bTickPhysicsAsync", "bSubsteppingAsync", "maxSubstepDeltaTime", "maxSubsteps", "asyncFixedTimeStepSize"):
            require(actual["physics_settings"][key] == source["physics_settings"][key], "Live PhysicsSettings differs: " + key)
    report = {"job_sha256": digest(job_file), "plan_sha256": digest(output/"plan.json"), "asset": path,
              "expected_counts": plan["expected_counts"], "max_anchor_error_cm": max_error,
              "collision_table": "Preserved by full asset duplication and unchanged body order; confirm mapped pairs/counts in editor",
              "verified_asset_sha256": digest(asset_file(project, path)),
              "candidate_sha256": digest(asset_file(project, state["candidate"])) if asset_file(project, state["candidate"]).exists() else state.get("candidate_verified_sha256")}
    write_json(output/"verification.json", report)
    if state["stage"] != "committed":
        state["stage"] = "verified"
        state["candidate_verified_sha256"] = report["candidate_sha256"]
        save_state(output, state)
    return report


def remap_pca(value, job, key=None):
    """Exact identity translations only; no substring replacement of arbitrary profile names."""
    mapping = dict(job["bone_map"], **job.get("reference_name_map", {}))
    bone_fields = {"startBone", "parentBoneName", "childBoneName", "boneName", "parentBone", "childBone"}
    if isinstance(value, dict):
        return {k: remap_pca(v, job, k) for k, v in value.items()}
    if isinstance(value, list):
        return [remap_pca(v, job, key) for v in value]
    if isinstance(value, str) and (key in bone_fields or key in ("name", "names")):
        return mapping.get(value, value)
    return value


def commit(job_file, job, project, output):
    checked_plan(job_file, job, output)
    state = read_json(state_file(output))
    require(state["stage"] == "verified", "Verify candidate immediately before commit")
    verification = read_json(output/"verification.json")
    require(verification["candidate_sha256"] == digest(asset_file(project, state["candidate"])), "Candidate changed since verification")
    target = job["target"]["pa"]
    clean([target, *([job["target"]["pca"]] if job["target"].get("pca") else [])])
    refs = list(unreal.EditorAssetLibrary.find_package_referencers_for_asset(target, True))
    require(not refs, "Automatic file replacement only supports an unreferenced target. Review an in-place/editor migration for: " + str(refs))
    original = asset_file(project, target)
    require(original.exists(), "Automatic commit currently requires an existing target PA")
    saved_files = state["backups"][str(original.relative_to(project))]
    for item in saved_files:
        require(digest(project/item["relative"]) == item["sha256"], "Target changed since backup")
    require(not original.with_suffix(".uexp").exists() and not original.with_suffix(".ubulk").exists(), "Sidecar PA package requires a dedicated replacement adapter")
    candidate_file = asset_file(project, state["candidate"])
    require(not candidate_file.with_suffix(".uexp").exists() and not candidate_file.with_suffix(".ubulk").exists(), "Candidate has sidecar files")
    pa = unreal.load_asset(target)
    unreal.get_editor_subsystem(unreal.AssetEditorSubsystem).close_all_editors_for_asset(pa)
    package = pa.get_outermost()
    pa = None
    unload(package)
    state["stage"] = "replacing_pa"
    save_state(output, state)
    try:
        shutil.copy2(candidate_file, original)
        final = unreal.load_asset(target)
        require(final is not None and len(final.get_constraints(True)) == verification["expected_counts"]["constraints"], "Final PA failed to load")
        require(unreal.EditorAssetLibrary.save_loaded_asset(final, False), "Final PA save failed")
    except Exception:
        # A partially loaded failed package may hold a reader. Release it before restoration.
        loaded = unreal.load_asset(target)
        if loaded:
            package = loaded.get_outermost()
            loaded = None
            final = None
            unload(package)
        shutil.copy2(output/"backup"/original.relative_to(project), original)
        require(unreal.load_asset(target) is not None, "Original PA restoration failed; keep backup and stop")
        state["stage"] = "pa_rolled_back"
        save_state(output, state)
        raise
    state["stage"] = "pa_committed"
    save_state(output, state)
    source = read_json(output/"source.json")
    target_pca = job["target"].get("pca")
    if target_pca:
        pca = unreal.load_asset(target_pca)
        require(pca is not None, "Target PCA missing")
        pca.modify()
        policy = job.get("pca_policy")
        require(policy in ("copy_source", "preserve_target"), "Choose pca_policy")
        if policy == "copy_source":
            require(source["pca"] is not None, "No source PCA snapshot")
            data = remap_pca(source["pca"], job)
            require(data.get("parentAsset") in (None, "None") and not data.get("additionalProfileAssets"),
                    "Inherited/additional PCA assets need explicit dependency migration")
            data["physicsAsset"] = {"refPath": final.get_path_name()}
            require(unreal.ToolsetLibrary.set_object_properties(pca, json.dumps(data)), "PCA property update failed")
        else:
            pca.set_editor_property("physics_asset", final)
        pca.call_method("Compile")
        require(not pca.call_method("IsCompilationNeeded"), "PCA compile failed")
        require(unreal.EditorAssetLibrary.save_loaded_asset(pca, False), "PCA save failed")
    if job.get("physics_settings_policy", "preserve_target") == "copy_source":
        require(job.get("allow_project_physics_settings") is True, "Global physics settings are outside job authorization")
        # Explicit key set only: do not overwrite project collision channels or all of DefaultEngine.ini.
        keymap = {"bSubstepping": "bSubstepping", "bTickPhysicsAsync": "bTickPhysicsAsync", "bSubsteppingAsync": "bSubsteppingAsync",
                  "maxSubstepDeltaTime": "MaxSubstepDeltaTime", "maxSubsteps": "MaxSubsteps", "asyncFixedTimeStepSize": "AsyncFixedTimeStepSize"}
        config = project/"Config/DefaultEngine.ini"
        destination = output/"backup/Config/DefaultEngine.ini"
        if not destination.exists():
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(config, destination)
            state["config"] = {"before_sha256": digest(config), "backup": str(destination)}
            save_state(output, state)
        lines = config.read_text(encoding="utf-8-sig").splitlines()
        section = "[/Script/Engine.PhysicsSettings]"
        indices = [i for i, line in enumerate(lines) if line.strip() == section]
        require(len(indices) <= 1, "Duplicate PhysicsSettings sections require a manual configuration merge")
        start = indices[0] if indices else len(lines)
        end = next((i for i in range(start+1, len(lines)) if lines[i].strip().startswith("[")), len(lines))
        values = {native: source["physics_settings"][script] for script, native in keymap.items()}
        content = [line for line in lines[start+1:end] if line.split("=", 1)[0].strip() not in values] if indices else []
        def text_value(value):
            return str(value) if isinstance(value, (bool, int)) else format(value, ".9g")
        block = [section, *content, *[key+"="+text_value(value) for key, value in values.items()], ""]
        config.write_text("\n".join([*lines[:start], *block, *lines[end:]])+"\n", encoding="utf-8")
        settings = unreal.get_default_object(unreal.PhysicsSettings)
        for script, native in keymap.items():
            settings.set_editor_property(native, source["physics_settings"][script])
        state["config"]["after_sha256"] = digest(config)
    state["stage"] = "committed"
    save_state(output, state)
    return verify(job_file, job, project, output)


def cleanup(job_file, job, project, output, discard=False):
    checked_plan(job_file, job, output)
    state = read_json(state_file(output))
    if discard:
        require(state["stage"] in ("preparing", "staged", "adapted", "verified"), "Cannot discard after formal replacement has started")
    else:
        require(state["stage"] == "committed", "Automatic cleanup is only enabled after a successful commit; failed jobs need a reviewed recovery")
    final = job["target"]["pa"]
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    dependencies = list(registry.get_dependencies(unreal.Name(final), unreal.AssetRegistryDependencyOptions()))
    require(not any(str(d).startswith(PREFIX) for d in dependencies), "Final asset still references temporary Content")
    retained = {final, job["target"].get("pca"), *([] if discard else job.get("materials", {}).values())}
    removed = []
    pending = {p for p in state["created"] if p not in retained and unreal.EditorAssetLibrary.does_asset_exist(p)}
    while pending:
        progress = False
        blocked = {}
        for path in sorted(pending):
            refs = list(unreal.EditorAssetLibrary.find_package_referencers_for_asset(path, True))
            if refs:
                blocked[path] = [str(r) for r in refs]
                continue
            require(unreal.EditorAssetLibrary.delete_asset(path), "Temporary deletion failed: " + path)
            pending.remove(path)
            removed.append(path)
            progress = True
            break
        require(progress, "Temporary assets still referenced; stop cleanup: " + str(blocked))
    state["cleanup"] = {"removed": removed, "dependencies": [str(d) for d in dependencies]}
    if discard:
        state["stage"] = "discarded"
    save_state(output, state)
    return state["cleanup"]


def execute(stage, job_file):
    job_file = Path(job_file).resolve(strict=True)
    job, output = load_job(job_file)
    side = "source" if stage in ("snapshot_source", "check_source") else "target"
    project = identity(job, side)
    output.mkdir(parents=True, exist_ok=True)
    if stage in ("stage", "adapt", "commit", "cleanup", "discard"):
        require(unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_game_world() is None,
                "Stop Play/Simulate-in-Editor before modifying migration assets")
    if stage.startswith("snapshot_"):
        require(not state_file(output).exists(), "Cannot overwrite original snapshots after staging")
        return capture(project, job[side], output, side)
    if stage == "check_source":
        data = read_json(output/"source.json")
        clean([item["package"] for item in data["binary_files"]])
        for item in data["binary_files"]:
            require(digest(project/item["relative"]) == item["sha256"], "Source changed since snapshot")
        return {"source_unchanged": True}
    functions = {"stage": prepare, "adapt": adapt, "verify": verify, "commit": commit, "cleanup": cleanup,
                 "discard": lambda *args: cleanup(*args, discard=True)}
    require(stage in functions, "Unknown migration stage")
    return functions[stage](job_file, job, project, output)


if globals().get("BOOTSTRAP_TOOLSET", True):
    unreal._codex_agent_pa_worker_path = str(Path(__file__).resolve())
    cached = getattr(unreal, "_codex_agent_pa_toolset", None)
    if cached is None:
        @unreal.uclass()
        class AgentPhysicsAssetTools(unreal.ToolsetDefinition):
            """Internal AGENTS physics asset operations for an explicit job."""
            @toolset_registry.tool_call
            @staticmethod
            def run(stage: str, job_file: str) -> str:
                """Run one internal migration stage; configuration is authored by the agent."""
                import runpy
                namespace = runpy.run_path(unreal._codex_agent_pa_worker_path, init_globals={"BOOTSTRAP_TOOLSET": False})
                result = namespace["execute"](stage, job_file)
                return json.dumps({"stage": stage, "status": "complete", "summary": result if stage in ("verify", "commit", "cleanup", "discard", "check_source") else None}, ensure_ascii=False)
        cached = AgentPhysicsAssetTools
        unreal._codex_agent_pa_toolset = cached
    unreal.ToolsetRegistry.unregister_toolset_class(cached)
    unreal.ToolsetRegistry.register_toolset_class(cached)
    unreal.SystemLibrary.execute_console_command(None, "ModelContextProtocol.RefreshTools")
    print("AGENT_PHYSICS_ASSET_TOOLS_READY")
