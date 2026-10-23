"""Twelve isolated synthetic sources; never opens a camera or changes the installed service."""
import argparse
import base64
import json
import os
from pathlib import Path
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", required=True)
    parser.add_argument("--engine", required=True)
    parser.add_argument("--probe", required=True)
    parser.add_argument("--desktop")
    parser.add_argument("--runtime-root")
    args = parser.parse_args()
    root = Path(args.root).resolve()
    output = root / "build/test-output"
    output.mkdir(parents=True, exist_ok=True)
    data = Path(tempfile.mkdtemp(prefix="multicamera-", dir=output))
    engine = str(Path(args.engine).resolve())
    runtime_root = Path(args.runtime_root).resolve() if args.runtime_root else root
    common = [engine, "--root", str(runtime_root), "--data", str(data)]
    runtime_env = dict(os.environ)
    if args.runtime_root:
        runtime_env.update(PATH=os.environ["SystemRoot"] + "\\System32;" + os.environ["SystemRoot"],
                           GST_PLUGIN_SYSTEM_PATH=str(runtime_root / "lib/gstreamer-1.0"),
                           GST_PLUGIN_SYSTEM_PATH_1_0=str(runtime_root / "lib/gstreamer-1.0"),
                           GST_PLUGIN_PATH="", GST_PLUGIN_PATH_1_0="",
                           GST_PLUGIN_SCANNER=str(runtime_root / "libexec/gstreamer-1.0/gst-plugin-scanner.exe"),
                           GST_REGISTRY=str(data / "gstreamer-registry.bin"))
    log = (data / "stderr.log").open("w", encoding="utf-8")
    process = subprocess.Popen(common + ["--console", "--synthetic", "--cameras", "12", "--run-seconds", "150"],
                               stdout=subprocess.DEVNULL, stderr=log, env=runtime_env)

    def request(command, *extra):
        result = subprocess.run(common + ["--request", command, *extra], capture_output=True,
                                text=True, encoding="utf-8", timeout=6, env=runtime_env)
        return json.loads(result.stdout) if result.stdout else {}

    def until(predicate, seconds=25):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            value = predicate()
            if value:
                return value
            time.sleep(0.3)
        raise AssertionError("Multi-camera state timed out; inspect " + str(data))

    try:
        until(lambda: request("status").get("runningCameras") == 12)
        media = []
        for n in range(1, 13):
            reply = until(lambda: (value if value.get("frames") else None)
                          if (value := request("live", "--camera", f"camera-{n}")) else None)
            assert reply["frames"][0]["key"], "Live stream must start at a keyframe"
            assert base64.b64decode(reply["frames"][0]["data"], validate=True)
            media.append({"camera": f"camera-{n}", "frames": len(reply["frames"])})
        events = until(lambda: (events if len({item["cameraId"] for item in events if item["status"] == "complete"}) == 12 else None)
                       if (events := request("events").get("events")) else None)
        for event in events:
            if event["status"] != "complete":
                continue
            manifest = request("playback", "--id", event["id"])
            assert manifest["ok"] and len(manifest["files"]) >= 2
            # Decode at least one finalized segment from every independent writer.
            subprocess.run([args.probe, "--validate", manifest["files"][0]], check=True,
                           stdout=subprocess.DEVNULL, timeout=15, env=runtime_env)
        grids = []
        if args.desktop:
            env = dict(os.environ, OPENNVR_DATA_DIR=str(data), QT_QPA_PLATFORM="offscreen")
            for count in (1, 4, 9, 12):
                with (data / f"grid-{count}.log").open("w", encoding="utf-8") as ui_log:
                    subprocess.run([str(Path(args.desktop).resolve()), "--ui-grid-test", "--grid-test-count", str(count)],
                                   cwd=root, env=env, stdout=subprocess.DEVNULL, stderr=ui_log, timeout=45, check=True)
                report = json.loads((root / f".local/ui-grid-{count}.json").read_text())
                assert report["success"] and len(report["videos"]) == count
                grids.append(report)
            assert request("status")["runningCameras"] == 12, "Closing the desktop interrupted camera workers"
        assert request("pause")["ok"]
        until(lambda: request("status").get("state") == "paused")
        assert request("resume")["ok"]
        until(lambda: request("status").get("runningCameras") == 12)
        assert request("shutdown")["ok"]
        process.wait(timeout=25)
        assert process.returncode == 0
        report = {"success": True, "sources": "12 synthetic H.264", "completeCameras": 12,
                  "live": media, "grids": grids,
                  "note": "Does not establish 12-camera RTSP/CUDA performance or soak acceptance"}
        report_name = "multicamera-bundle-validation.json" if args.runtime_root else "multicamera-validation.json"
        (root / ".local" / report_name).write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
        print("PASS 12 camera workers/media pipes, independent events/decoded segments, pause/resume" +
              (", grids 1/4/9/12 and desktop detach" if grids else " (headless)"))
    finally:
        if process.poll() is None:
            process.terminate()
            process.wait(timeout=10)
        log.close()


if __name__ == "__main__":
    main()
