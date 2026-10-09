"""Create a platform-named CLI archive without overwriting other platforms."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import tempfile
import tomllib
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("--target", required=True)
parser.add_argument("--build", default="build")
parser.add_argument("--output", default="dist")
args = parser.parse_args()
root = Path(__file__).resolve().parents[2]
version = tomllib.loads((root / "photon/bindings/python/pyproject.toml").read_text())["project"]["version"]
output = Path(args.output)
output.mkdir(parents=True, exist_ok=True)
archive = output / f"quantum-stack-{version}-{args.target}.zip"
with tempfile.TemporaryDirectory(prefix="qstack-release-") as temp:
    stage = Path(temp) / f"quantum-stack-{version}-{args.target}"
    (stage / "bin").mkdir(parents=True)
    hashes = {}
    for name in ("spinorc", "phononc", "photonc", "photonc-cxx"):
        candidates = [p for p in Path(args.build).rglob(name + ("*.exe" if "windows" in args.target else ""))
                      if p.is_file() and p.name in {name, name + ".exe"}
                      and ("cli" in p.parts or name == "photonc-cxx")]
        candidates = sorted(set(candidates), key=lambda p: (len(p.parts), str(p)))
        if not candidates:
            raise SystemExit(f"Missing release compiler: {name}")
        destination = stage / "bin" / candidates[0].name
        shutil.copy2(candidates[0], destination)
        hashes[destination.name] = hashlib.sha256(destination.read_bytes()).hexdigest()
    for folder in ("chips", "topologies"):
        shutil.copytree(root / "spinor/registry" / folder, stage / "registry" / folder)
    shutil.copy2(root / "LICENSE", stage / "LICENSE")
    (stage / "README.txt").write_text(
        "Add bin/ to PATH and set SPINOR_REGISTRY_ROOT to this archive's registry/ directory.\n"
        "Install heisenberg-spinor-submit=="+version+" for qstack execution/provider commands.\n"
        "Native executables require the platform's C/C++ runtime. The Python wheel includes its required Windows DLLs.\n"
        "Compiling and local simulation do not require provider credentials.\n", encoding="utf-8")
    (stage / "manifest.json").write_text(json.dumps({"version": version, "platform": args.target,
        "sha256": hashes}, indent=2) + "\n", encoding="utf-8")
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as bundle:
        for file in sorted(stage.rglob("*")):
            if file.is_file():
                bundle.write(file, file.relative_to(stage.parent))
(archive.with_suffix(".zip.sha256")).write_text(hashlib.sha256(archive.read_bytes()).hexdigest()+
    "  "+archive.name+"\n", encoding="utf-8")
print(archive)
