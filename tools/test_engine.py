"""Isolated engine/IPC integration test. Never uses camera credentials or real video."""

import argparse
import json
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", required=True)
    parser.add_argument("--probe", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    Path(args.output).mkdir(parents=True, exist_ok=True)
    directory = Path(tempfile.mkdtemp(prefix="engine-", dir=args.output)).resolve()
    common = [args.engine, "--root", args.root, "--data", str(directory)]
    process = None

    def launch():
        return subprocess.Popen(common + ["--console", "--synthetic", "--run-seconds", "50"],
                                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def request(command, *extra):
        result = subprocess.run(common + ["--request", command, *extra], capture_output=True,
                                text=True, encoding="utf-8", timeout=5)
        try:
            return json.loads(result.stdout)
        except json.JSONDecodeError:
            return {"ok": False}

    def until(predicate, seconds=15):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            time.sleep(0.2)
        raise AssertionError("Timed out waiting for engine state")

    try:
        process = launch()
        until(lambda: request("status").get("state") == "running")
        duplicate = subprocess.run(common + ["--console", "--synthetic", "--run-seconds", "1"],
                                   capture_output=True, timeout=10)
        assert duplicate.returncode != 0, "duplicate engine was allowed"
        assert request("pause")["ok"]
        until(lambda: request("status").get("state") == "paused")
        assert request("resume")["ok"]
        until(lambda: request("status").get("state") == "running")
        events = until(lambda: [event for event in request("events").get("events", [])
                               if event["status"] == "complete"], seconds=18)
        event = events[0]
        manifest = request("playback", "--id", event["id"])
        assert manifest["ok"] and len(manifest["files"]) >= 2, "segment rotation not indexed"
        for file in manifest["files"]:
            assert Path(file).is_file()
            subprocess.run([args.probe, "--validate", file], check=True, timeout=20)
        assert request("shutdown")["ok"]
        process.wait(timeout=20)
        assert process.returncode == 0, "graceful engine shutdown failed"

        # Kill only this test's own child while a synthetic event is active.
        process = launch()
        active = until(lambda: [event for event in request("events").get("events", [])
                               if event["status"] == "recording"], seconds=15)[0]
        process.terminate()
        process.wait(timeout=10)
        process = launch()
        recovered = until(lambda: next((event for event in request("events").get("events", [])
                                        if event["id"] == active["id"] and event["status"] != "recording"), None))
        assert recovered["status"] in {"interrupted", "partial"}
        old = request("playback", "--id", event["id"])
        assert old["ok"] and old["files"], "finalized files lost after crash"
        assert request("shutdown")["ok"]
        process.wait(timeout=20)
        print("PASS engine singleton, pause/resume, IPC, segmentation, playback and crash recovery")
    finally:
        if process is not None and process.poll() is None:
            process.terminate()
            process.wait(timeout=10)


if __name__ == "__main__":
    main()
