"""Explicit real-camera check: engine outlives desktop and the old 60s limit."""

import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    args = parser.parse_args()
    root = Path(args.root).resolve()
    directory = Path(tempfile.mkdtemp(prefix="desktop-detach-", dir=root / ".local"))
    executable = root / "build/dev/opennvr_engine.exe"
    common = [str(executable), "--root", str(root), "--data", str(directory)]
    process = subprocess.Popen(common + ["--console", "--run-seconds", "100"],
                               stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def status():
        result = subprocess.run(common + ["--request", "status"], capture_output=True,
                                text=True, encoding="utf-8", timeout=5)
        return json.loads(result.stdout)

    try:
        deadline = time.monotonic() + 25
        while time.monotonic() < deadline:
            ready = status()
            if ready.get("state") == "running" and ready.get("aiReady"):
                break
            time.sleep(0.3)
        else:
            raise AssertionError("Camera engine did not become ready")
        began = time.monotonic()
        environment = os.environ.copy()
        environment.update({"OPENNVR_DATA_DIR": str(directory), "QT_QPA_PLATFORM": "offscreen", "QT_QUICK_BACKEND": "software"})
        subprocess.run([str(root / "build/dev/opennvr.exe"), "--smoke-test"], cwd=root,
                       env=environment, check=True, timeout=15)
        after_close = status()
        assert after_close.get("state") == "running" and process.poll() is None
        while time.monotonic() - began < 65:
            assert process.poll() is None, "Engine exited when the desktop closed"
            time.sleep(1)
        final = status()
        assert final.get("state") == "running" and final.get("analyzedFrames", 0) > after_close.get("analyzedFrames", 0)
        report = {"success": True, "desktopExited": True, "continuedSeconds": round(time.monotonic() - began, 1),
                  "framesAtDesktopClose": after_close.get("analyzedFrames"), "framesLater": final.get("analyzedFrames"),
                  "service": final.get("service"), "state": final.get("state")}
        (root / ".local/desktop-detach-validation.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print(json.dumps(report))
        subprocess.run(common + ["--request", "shutdown"], check=True, capture_output=True, timeout=5)
        process.wait(timeout=20)
        assert process.returncode == 0
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)


if __name__ == "__main__":
    main()
