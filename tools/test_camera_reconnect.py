"""Explicit, local camera recovery test using an existing DPAPI credential file.

Creates an isolated engine data directory, first points at loopback, then restores
the real encrypted camera configuration. Does not change camera/network settings.
"""

import argparse
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--engine", required=True)
    parser.add_argument("--root", required=True)
    parser.add_argument("--runtime-root")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    source = root / ".local/camera.json"
    if not source.is_file():
        raise SystemExit("Configure a camera locally before this test")
    directory = Path(tempfile.mkdtemp(prefix="camera-reconnect-", dir=root / ".local"))
    credential = json.loads(source.read_text(encoding="utf-8-sig"))
    credential["host"] = "127.0.0.1"
    (directory / "camera.json").write_text(json.dumps(credential), encoding="utf-8")
    del credential
    runtime_root = str(Path(args.runtime_root).resolve()) if args.runtime_root else str(root)
    common = [args.engine, "--root", runtime_root, "--data", str(directory)]
    error_log = (directory / "stderr.log").open("w", encoding="utf-8")
    process = subprocess.Popen(common + ["--console", "--run-seconds", "65"],
                               stdout=subprocess.DEVNULL, stderr=error_log)

    def request(command):
        response = subprocess.run(common + ["--request", command], capture_output=True,
                                  text=True, encoding="utf-8", timeout=5)
        return json.loads(response.stdout)

    def until(predicate, seconds):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            result = predicate(request("status"))
            if result:
                return result
            time.sleep(0.3)
        raise AssertionError("Camera engine did not reach the expected state")

    try:
        failed = until(lambda status: status if status.get("state") == "reconnecting" else None, 25)
        shutil.copy2(source, directory / "camera.json")
        restored = time.monotonic()
        recovered = until(lambda status: status if status.get("state") == "running" and status.get("aiReady") else None, 35)
        elapsed = (time.monotonic() - restored) * 1000
        report = {"success": True, "recoveryMs": round(elapsed, 1),
                  "failedAttempt": failed.get("attempts"), "recoveredAttempt": recovered.get("attempts"),
                  "analyzedFrames": recovered.get("analyzedFrames"), "service": recovered.get("service"),
                  "note": "Restored encrypted config after an intentionally unreachable loopback endpoint"}
        (root / ".local/reconnect-validation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report))
        assert elapsed <= 30000, "Recovery exceeded the initial 30-second target"
        assert request("shutdown")["ok"]
        process.wait(timeout=20)
        assert process.returncode == 0
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        error_log.close()


if __name__ == "__main__":
    main()
