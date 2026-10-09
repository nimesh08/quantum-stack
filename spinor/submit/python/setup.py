"""Ship the authoritative target registry in both wheels and source archives."""
from pathlib import Path

from setuptools import setup
from setuptools.command.build_py import build_py
from setuptools.command.sdist import sdist

PACKAGE_ROOT = Path(__file__).resolve().parent


def registry_source() -> Path:
    # A checkout uses the single maintained registry. A source distribution
    # carries its own copy so rebuilding never depends on a parent checkout.
    checkout = PACKAGE_ROOT.parents[1] / "registry"
    bundled = PACKAGE_ROOT / "qstack" / "data" / "registry"
    for candidate in (checkout, bundled):
        if (candidate / "chips").is_dir() and (candidate / "topologies").is_dir():
            return candidate
    raise RuntimeError("Target registry is missing from the source distribution")


def copy_registry(destination: Path) -> list[str]:
    source = registry_source()
    paths = []
    for directory in ("chips", "topologies"):
        target = destination / "qstack" / "data" / "registry" / directory
        target.mkdir(parents=True, exist_ok=True)
        for original in sorted((source / directory).glob("*.yaml")):
            (target / original.name).write_bytes(original.read_bytes())
            paths.append((target / original.name).relative_to(destination).as_posix())
    license_source = PACKAGE_ROOT.parents[2] / "LICENSE"
    if not license_source.is_file():
        license_source = PACKAGE_ROOT / "qstack" / "LICENSE"
    if not license_source.is_file():
        raise RuntimeError("License text is missing from the source distribution")
    license_destination = destination / "qstack" / "LICENSE"
    license_destination.write_bytes(license_source.read_bytes())
    paths.append("qstack/LICENSE")
    return paths


class BuildWithRegistry(build_py):
    def run(self):
        super().run()
        copy_registry(Path(self.build_lib))


class SourceWithRegistry(sdist):
    def make_release_tree(self, base_dir, files):
        super().make_release_tree(base_dir, files)
        base = Path(base_dir)
        added = copy_registry(base)
        # Keep the manifest correct when this archive is rebuilt or inspected.
        for manifest in base.glob("*.egg-info/SOURCES.txt"):
            entries = set(manifest.read_text(encoding="utf-8").splitlines())
            manifest.write_text("\n".join(sorted(entries | set(added))) + "\n", encoding="utf-8")


setup(cmdclass={"build_py": BuildWithRegistry, "sdist": SourceWithRegistry})
