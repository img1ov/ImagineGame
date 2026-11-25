"""Single entry point; all mutations are explicit stages, never an automatic all command."""
import argparse
import json
import sys
import urllib.error
from pathlib import Path
sys.dont_write_bytecode = True
from mapping import build_plan, comparison, differences, digest, load_job, read_json, write_json
from mcp_client import Client


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("job", type=Path, help="Reviewed job JSON; absolute project paths inside")
    parser.add_argument("stage", choices=["snapshot", "plan", "stage", "adapt", "verify", "commit", "cleanup", "discard"])
    args = parser.parse_args()
    args.job = args.job.resolve(strict=True)
    job, output = load_job(args.job)
    if args.stage == "plan":
        source, target = read_json(output/"source.json"), read_json(output/"target.json")
        report = comparison(job, source, target)
        def physics_cvars(side):
            path = output/(side+"_cvars.json")
            if not path.exists():
                return None
            payload = read_json(path)
            value = json.loads(next(c["text"] for c in payload["content"] if c["type"] == "text"))["returnValue"]
            value = json.loads(value) if isinstance(value, str) else value
            return {k: v["value"] for k, v in value.items() if k.startswith("p.")}
        a, b = physics_cvars("source"), physics_cvars("target")
        report["physics_cvars"] = differences(a, b) if a is not None and b is not None else {"status": "missing snapshot"}
        write_json(output/"comparison.json", report)
        plan = build_plan(job, source, target)
        plan["job_sha256"] = digest(args.job)
        plan["snapshot_sha256"] = {s: digest(output/(s+".json")) for s in ("source", "target")}
        write_json(output/"plan.json", plan)
        print(json.dumps({"plan": str(output/"plan.json"), "comparison": str(output/"comparison.json"), "sha256": digest(output/"plan.json"),
                          "counts": plan["expected_counts"], "warnings": plan["warnings"]}, ensure_ascii=False, indent=2))
        return
    sides = ("source", "target") if args.stage == "snapshot" else ("target",)
    if args.stage == "stage":
        with Client(job["source"]["port"]) as client:
            client.worker("check_source", args.job)
    for side in sides:
        with Client(job[side]["port"]) as client:
            stage = "snapshot_"+side if args.stage == "snapshot" else args.stage
            result = client.worker(stage, args.job)
            print(side, json.dumps(result, ensure_ascii=False))
            if args.stage == "snapshot":
                cvars = client.call("EditorToolset.EditorAppToolset", "SearchCVars", {"name": "p."})
                write_json(output/(side+"_cvars.json"), cvars)


if __name__ == "__main__":
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="backslashreplace")
    try:
        main()
    except (RuntimeError, ValueError, KeyError, OSError, urllib.error.URLError) as error:
        print("Migration stopped: " + str(error), file=sys.stderr)
        sys.exit(1)
