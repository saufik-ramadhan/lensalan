"""Fetch pinned development AI dependencies without changing system CUDA/Python.

Uses official GitHub releases and NVIDIA's PyPI wheels. All files stay in .deps/.
CUDA wheels are DLL containers here; Python is not part of the app runtime.
"""

from concurrent.futures import ThreadPoolExecutor
import hashlib
import json
from pathlib import Path
import sys
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DEPS = ROOT / ".deps"
DOWNLOADS = DEPS / "downloads"


def get_json(url):
    request = urllib.request.Request(url, headers={"User-Agent": "OpenNVR-development"})
    with urllib.request.urlopen(request, timeout=60) as response:
        return json.load(response)


def download(url, name, digest=None):
    DOWNLOADS.mkdir(parents=True, exist_ok=True)
    target = DOWNLOADS / name
    if target.exists() and digest:
        with target.open("rb") as source:
            if hashlib.file_digest(source, "sha256").hexdigest() == digest:
                return target
    elif target.exists():
        return target
    print(f"Downloading {name}", flush=True)
    temporary = target.with_suffix(target.suffix + ".part")
    request = urllib.request.Request(url, headers={"User-Agent": "OpenNVR-development"})
    with urllib.request.urlopen(request, timeout=120) as response, temporary.open("wb") as output:
        while chunk := response.read(1024 * 1024):
            output.write(chunk)
    if digest:
        with temporary.open("rb") as source:
            actual = hashlib.file_digest(source, "sha256").hexdigest()
        if actual != digest:
            raise RuntimeError(f"SHA256 mismatch: {name}")
    temporary.replace(target)
    return target


def extract(archive, destination):
    destination.mkdir(parents=True, exist_ok=True)
    root = destination.resolve()
    with zipfile.ZipFile(archive) as source:
        for item in source.infolist():
            resolved = (root / item.filename).resolve()
            if not resolved.is_relative_to(root):
                raise RuntimeError(f"Unsafe archive member in {archive.name}")
        source.extractall(root)


def cuda_package(name, version):
    metadata = get_json(f"https://pypi.org/pypi/{name}/{version}/json")
    wheel = next(item for item in metadata["urls"] if item["filename"].endswith("win_amd64.whl"))
    destination = DEPS / "cuda" / name
    marker = destination / ".complete"
    if marker.exists() and marker.read_text().strip() == version:
        return
    archive = download(wheel["url"], wheel["filename"], wheel["digests"]["sha256"])
    extract(archive, destination)
    marker.write_text(version, encoding="utf-8")
    print(f"Ready: {name} {version}", flush=True)


def main():
    if sys.platform != "win32":
        raise SystemExit("This development bundle targets Windows x64.")
    runtime_name = "onnxruntime-win-x64-gpu-1.23.2.zip"
    if not (DEPS / "onnxruntime-win-x64-gpu-1.23.2" / "include" / "onnxruntime_c_api.h").exists():
        metadata = get_json("https://api.github.com/repos/microsoft/onnxruntime/releases/tags/v1.23.2")
        asset = next(item for item in metadata["assets"] if item["name"] == runtime_name)
        digest = (asset.get("digest") or "").removeprefix("sha256:") or None
        extract(download(asset["browser_download_url"], runtime_name, digest), DEPS)
    model = DEPS / "models" / "yolox_tiny.onnx"
    model.parent.mkdir(parents=True, exist_ok=True)
    if not model.exists():
        metadata = get_json("https://api.github.com/repos/Megvii-BaseDetection/YOLOX/releases/tags/0.1.1rc0")
        asset = next(item for item in metadata["assets"] if item["name"] == "yolox_tiny.onnx")
        source = download(asset["browser_download_url"], "yolox_tiny.onnx")
        model.write_bytes(source.read_bytes())
    model_digest = "427cc366d34e27ff7a03e2899b5e3671425c262ea2291f88bb942bc1cc70b0f7"
    with model.open("rb") as source:
        if hashlib.file_digest(source, "sha256").hexdigest() != model_digest:
            raise RuntimeError("Unexpected YOLOX-Tiny model SHA256")
    image = DEPS / "models" / "basketball1.png"
    if not image.exists():
        source = download("https://raw.githubusercontent.com/opencv/opencv/4.12.0/samples/data/basketball1.png",
                          "basketball1.png", "ba06f6701f7260998b430c39b6557f775497e6ce7b1a74f0b7ea6af371bf54a6")
        image.write_bytes(source.read_bytes())
    with ThreadPoolExecutor(max_workers=3) as pool:
        list(pool.map(lambda item: cuda_package(*item), [
            ("nvidia-cublas-cu12", "12.9.1.4"),
            ("nvidia-cudnn-cu12", "9.10.2.21"),
            ("nvidia-cuda-runtime-cu12", "12.9.79"),
            ("nvidia-cufft-cu12", "11.4.1.4"),
        ]))
    notice = DEPS / "models" / "YOLOX-LICENSE.txt"
    if not notice.exists():
        source = download("https://raw.githubusercontent.com/Megvii-BaseDetection/YOLOX/main/LICENSE", "YOLOX-LICENSE.txt")
        notice.write_bytes(source.read_bytes())
    with model.open("rb") as source:
        model_hash = hashlib.file_digest(source, "sha256").hexdigest()
    manifest = {"runtime": "1.23.2", "model": model.name, "modelSha256": model_hash,
                "cuda": "12.9", "cudnn": "9.10.2.21"}
    (DEPS / "ai-manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("AI development bundle ready in .deps/", flush=True)


if __name__ == "__main__":
    main()
