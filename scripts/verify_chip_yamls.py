#!/usr/bin/env python3
"""Verify every chip YAML in ``spinor/registry/chips/``.

Usage::

    python3 scripts/verify_chip_yamls.py
    python3 scripts/verify_chip_yamls.py --strict

Exit code is 0 if every chip is well-formed; non-zero on the first
failure (per ``--strict``) or after collecting all failures.

Checks performed:

1.  Every YAML parses cleanly.
2.  Required keys are present.
3.  ``provider`` is one of the known providers (post-Step-2:
    ``ibm aws azure local qci anyon tii alicebob``).
4.  Every gate in ``native_gates`` is one the lexer accepts. The
    accepted set is read directly from
    ``spinor/parser/cpp/lib/Lexer.cpp`` so the check stays canonical.
5.  ``coupling_map.topology`` resolves to either a built-in
    (``linear_n``, ``all_to_all``) or a topology YAML on disk.
6.  ``decomposition`` is internally consistent (``recipe == euler_zyz``
    for one-qubit, ``recipe == kak`` for two-qubit, entangler is in
    the lexer's vocabulary, ``entangler_count_max == 3``).
7.  ``notes`` contains a ``source:`` URL and a
    ``verified-upstream: YYYY-MM-DD`` line.
8.  Smoke compile (only if ``spinorc`` is on ``PATH`` or
    ``$SPINORC_BIN``): compile a tiny Bell program against each chip
    and assert success or that failure is a precise W6 / W7 because
    the chip does not have a needed gate.
"""

from __future__ import annotations

import argparse
import dataclasses
import os
import pathlib
import re
import shutil
import subprocess
import sys
import tempfile
from typing import Iterable

import yaml

REPO_ROOT = pathlib.Path(__file__).resolve().parents[1]
CHIPS_DIR = REPO_ROOT / "spinor" / "registry" / "chips"
TOP_DIR = REPO_ROOT / "spinor" / "registry" / "topologies"
LEXER_CPP = REPO_ROOT / "spinor" / "parser" / "cpp" / "lib" / "Lexer.cpp"

KNOWN_PROVIDERS = {
    "ibm", "aws", "azure", "local",
    "qci", "anyon", "tii", "alicebob",
}

REQUIRED_TOP_KEYS = {
    "id", "provider", "qubits", "native_gates",
    "coupling_map", "supports", "calibration",
    "decomposition", "pricing", "notes",
}

BUILTIN_TOPOLOGIES = {"linear_n", "all_to_all"}


@dataclasses.dataclass
class Failure:
    chip: str
    message: str


def read_lexer_gates() -> set[str]:
    """Read the gate mnemonic set straight from ``Lexer.cpp``.

    Looks for the static ``s = { ... }`` initialiser inside
    ``gateMnemonics()``. Strings between double quotes inside that
    block are the accepted mnemonics.
    """
    src = LEXER_CPP.read_text()
    m = re.search(
        r"gateMnemonics\(\)\s*\{[^{]*?\{(.*?)\};",
        src, re.DOTALL,
    )
    if not m:
        raise RuntimeError("could not find gateMnemonics() in Lexer.cpp")
    body = m.group(1)
    return set(re.findall(r'"([^"]+)"', body))


def topology_exists(name: str) -> bool:
    if name in BUILTIN_TOPOLOGIES:
        return True
    return (TOP_DIR / f"{name}.yaml").exists()


def check_one(path: pathlib.Path, lexer_gates: set[str]) -> list[Failure]:
    cid = path.stem
    fails: list[Failure] = []
    try:
        data = yaml.safe_load(path.read_text())
    except yaml.YAMLError as e:
        return [Failure(cid, f"YAML parse error: {e}")]
    if not isinstance(data, dict):
        return [Failure(cid, "top-level must be a mapping")]
    missing = REQUIRED_TOP_KEYS - set(data.keys())
    if missing:
        fails.append(Failure(cid, f"missing keys: {sorted(missing)}"))
    if data.get("id") != cid:
        fails.append(Failure(
            cid, f"id={data.get('id')!r} does not match filename"))
    provider = data.get("provider")
    if provider not in KNOWN_PROVIDERS:
        fails.append(Failure(
            cid, f"provider {provider!r} not in {sorted(KNOWN_PROVIDERS)}"))
    qubits = data.get("qubits")
    if not isinstance(qubits, int) or qubits <= 0:
        fails.append(Failure(cid, f"qubits must be a positive int (got {qubits!r})"))
    nat = data.get("native_gates") or []
    if not nat:
        fails.append(Failure(cid, "native_gates is empty"))
    for g in nat:
        if g not in lexer_gates:
            fails.append(Failure(
                cid, f"native_gates contains {g!r} but lexer only accepts "
                     f"{sorted(lexer_gates)}"))
    cm = data.get("coupling_map") or {}
    top = cm.get("topology")
    if not top:
        fails.append(Failure(cid, "coupling_map.topology missing"))
    elif not topology_exists(top):
        fails.append(Failure(
            cid, f"coupling_map.topology {top!r} resolves to no file under "
                 f"{TOP_DIR.relative_to(REPO_ROOT)} and is not a built-in"))
    size = cm.get("size")
    if size is not None and qubits is not None and size != qubits:
        fails.append(Failure(
            cid, f"coupling_map.size={size} != qubits={qubits}"))
    dec = data.get("decomposition") or {}
    one = dec.get("one_qubit") or {}
    two = dec.get("two_qubit") or {}
    if one.get("recipe") != "euler_zyz":
        fails.append(Failure(
            cid, f"decomposition.one_qubit.recipe must be 'euler_zyz'"))
    if two.get("recipe") != "kak":
        fails.append(Failure(
            cid, f"decomposition.two_qubit.recipe must be 'kak'"))
    ent = two.get("entangler")
    if ent and ent not in lexer_gates:
        fails.append(Failure(
            cid, f"entangler {ent!r} not in lexer set"))
    if two.get("entangler_count_max") != 3:
        fails.append(Failure(
            cid, "decomposition.two_qubit.entangler_count_max must be 3"))
    pr = data.get("pricing") or {}
    if "per_shot_usd" not in pr:
        fails.append(Failure(cid, "pricing.per_shot_usd missing"))
    notes = str(data.get("notes") or "")
    if "source:" not in notes:
        fails.append(Failure(cid, "notes missing 'source:' citation"))
    if not re.search(r"verified-upstream:\s*\d{4}-\d{2}-\d{2}", notes):
        fails.append(Failure(
            cid, "notes missing 'verified-upstream: YYYY-MM-DD'"))
    return fails


BELL_TEMPLATE = """; Bell pair smoke test (target stamped per chip)
target {chip}
qubit q[2]
bit c[2]
h q[0]
cx q[0], q[1]
c = measure q
"""


def find_spinorc() -> str | None:
    candidate = os.environ.get("SPINORC_BIN")
    if candidate and pathlib.Path(candidate).exists():
        return candidate
    on_path = shutil.which("spinorc")
    if on_path:
        return on_path
    here = REPO_ROOT / "build" / "spinor" / "cli" / "spinorc"
    if here.exists():
        return str(here)
    return None


def smoke_compile(chips: Iterable[str], spinorc: str) -> tuple[list[Failure], list[Failure]]:
    """Attempt ``spinorc compile -t <chip> bell.spn`` for each chip.

    Returns ``(hard_failures, soft_failures)``:

    - **Hard** failures: anything we'd consider a real bug (registry
      mismatch, parser error, etc.).
    - **Soft** failures: known compiler limitations - currently the
      KAK two-qubit decomposer only ships recipes for ``ecr``, ``ms``
      and ``rzz`` entanglers; chips with ``cz`` or ``cx`` entanglers
      crash with ``emitCX: no recipe for entangler '<gate>'``. That's
      a Phase A code change tracked in chips_unsupported.md, not a
      YAML problem. The script reports them but does not fail the
      build.

    A negative path that mentions ``W6`` or ``W7`` (the legality and
    entangler-vocabulary verifier rules) is *also* OK because it just
    means the chip lacks the gate the program uses; we want the
    negative path to be clean too.
    """
    hard: list[Failure] = []
    soft: list[Failure] = []
    env = dict(os.environ)
    env["SPINOR_REGISTRY_ROOT"] = str(REPO_ROOT / "spinor" / "registry")
    with tempfile.TemporaryDirectory() as td:
        tdp = pathlib.Path(td)
        for cid in chips:
            bell = tdp / f"bell_{cid}.spn"
            bell.write_text(BELL_TEMPLATE.format(chip=cid))
            try:
                r = subprocess.run(
                    [spinorc, "compile", "-t", cid, str(bell)],
                    capture_output=True, text=True, env=env, timeout=60,
                )
            except (subprocess.TimeoutExpired, FileNotFoundError) as e:
                hard.append(Failure(cid, f"spinorc invocation failed: {e}"))
                continue
            if r.returncode == 0:
                continue
            err = r.stderr or ""
            if "W6" in err or "W7" in err:
                continue
            if "emitCX: no recipe for entangler" in err:
                soft.append(Failure(
                    cid, "compiler does not yet ship a KAK-CZ/CX recipe "
                         "(known gap; tracked in chips_unsupported.md)"))
                continue
            hard.append(Failure(
                cid,
                f"spinorc compile failed unexpectedly (exit={r.returncode}): "
                f"{err.strip().splitlines()[-1] if err.strip() else 'no stderr'}"))
    return hard, soft


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--strict", action="store_true",
                        help="exit on first failure")
    parser.add_argument("--no-smoke", action="store_true",
                        help="skip the spinorc smoke compile")
    args = parser.parse_args()

    lexer_gates = read_lexer_gates()
    print(f"lexer accepts {len(lexer_gates)} gate mnemonics: "
          f"{sorted(lexer_gates)}")

    chips = sorted(CHIPS_DIR.glob("*.yaml"))
    print(f"checking {len(chips)} chip YAML files in "
          f"{CHIPS_DIR.relative_to(REPO_ROOT)}")
    all_fails: list[Failure] = []
    for p in chips:
        fails = check_one(p, lexer_gates)
        for f in fails:
            print(f"  [FAIL] {f.chip}: {f.message}")
            if args.strict:
                return 1
        all_fails.extend(fails)

    if not args.no_smoke:
        spinorc = find_spinorc()
        if spinorc is None:
            print("(skipping spinorc smoke compile - binary not found)")
        else:
            print(f"smoke compile via {spinorc}")
            chip_ids = [p.stem for p in chips]
            hard, soft = smoke_compile(chip_ids, spinorc)
            for f in hard:
                print(f"  [SMOKE FAIL] {f.chip}: {f.message}")
            for f in soft:
                print(f"  [known-gap]  {f.chip}: {f.message}")
            all_fails.extend(hard)

    if all_fails:
        print(f"\n{len(all_fails)} hard failures across {len(chips)} chips")
        return 1
    print(f"\nok: {len(chips)} chip YAMLs passed schema + topology checks")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
