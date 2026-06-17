"""``python -m spinor_submit`` entry point.

Argparse front over the existing :func:`spinor_submit.submit`. Used by
the C++ drivers (photonc / phononc / spinorc) which subprocess into
this module so the vendor SDKs (qiskit-ibm-runtime, amazon-braket-sdk,
azure-quantum) handle their own auth chains.

Subcommands (matches the C++ binaries' surface):

- ``submit``     send a QASM3 file to a provider
- ``targets``    list registry chips
- ``providers``  list providers + their flag conventions
- ``version``    print version

Example::

    python -m spinor_submit submit \\
        --qasm-file out.qasm3 \\
        --chip ibm_heron_r2 \\
        --provider ibm \\
        --shots 1000 \\
        --mode cassette \\
        --program-name bell

Run ``python -m spinor_submit --help`` for the full flag set.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
import pathlib
from typing import Sequence

from . import SUPPORTED_PROVIDERS, submit, __version__ as _pkg_version


def _read_secret(args: argparse.Namespace) -> str | None:
    """Resolve the API key per the open-cli-collective standard.

    Precedence: --api-key-file > --api-key-stdin > env var.
    Never accept --api-key=<literal> (the parent C++ CLI rejects that
    earlier; this is just a defence in depth).
    """
    if args.api_key_file:
        try:
            return pathlib.Path(args.api_key_file).read_text().strip()
        except OSError as e:
            print(f"spinor_submit: cannot read api-key-file: {e}",
                  file=sys.stderr)
            sys.exit(1)
    if args.api_key_stdin:
        return sys.stdin.readline().strip()
    return None


def _cmd_submit(args: argparse.Namespace) -> int:
    if args.provider not in SUPPORTED_PROVIDERS:
        print(f"spinor_submit: unknown provider {args.provider!r} "
              f"(known: {sorted(SUPPORTED_PROVIDERS)})",
              file=sys.stderr)
        return 2

    # Honour the secret so the live SDK can pick it up via env. We
    # don't write a literal value into the program env; we only
    # populate the canonical IBM/AWS/Azure variable when --api-key-file
    # / --api-key-stdin was used.
    secret = _read_secret(args)
    if secret:
        if args.provider == "ibm":
            os.environ.setdefault("IBM_QUANTUM_TOKEN", secret)
        elif args.provider == "aws":
            os.environ.setdefault("AWS_SESSION_TOKEN", secret)
        elif args.provider == "azure":
            os.environ.setdefault("AZURE_QUANTUM_RESOURCE_ID", secret)

    # Forward any --extra k=v pairs to env for the vendor SDKs that
    # honour them (Azure workspace, IonQ region, etc.). The escape
    # hatch the open-cli-collective recommends for provider-specific
    # knobs we haven't elevated to a first-class flag.
    for kv in args.extra or []:
        if "=" in kv:
            k, v = kv.split("=", 1)
            os.environ.setdefault(k, v)

    if args.url:
        os.environ.setdefault("SPINOR_PROVIDER_URL", args.url)
    if args.region:
        os.environ.setdefault("AWS_DEFAULT_REGION", args.region)
    if args.instance:
        os.environ.setdefault("IBM_QUANTUM_INSTANCE", args.instance)

    os.environ.setdefault("SPINOR_SUBMIT_MODE", args.mode)

    if args.qasm_file == "-":
        qasm = sys.stdin.read()
    else:
        try:
            qasm = pathlib.Path(args.qasm_file).read_text()
        except OSError as e:
            print(f"spinor_submit: cannot read {args.qasm_file}: {e}",
                  file=sys.stderr)
            return 1

    program_name = args.program_name
    if not program_name:
        program_name = (
            pathlib.Path(args.qasm_file).stem
            if args.qasm_file != "-" else "stdin"
        )

    try:
        hist = submit(
            qasm,
            chip=args.chip,
            provider=args.provider,
            shots=args.shots,
            program_name=program_name,
        )
    except Exception as e:
        print(f"spinor_submit: submission failed: {e}", file=sys.stderr)
        return 1

    if args.json:
        print(json.dumps({
            "shots": hist.shots,
            "counts": dict(hist.counts),
            "total":  sum(hist.counts.values()),
        }, indent=2))
        return 0

    total = sum(hist.counts.values())
    print(f"shots={hist.shots} total counts={total}")
    print("histogram:")
    for k in sorted(hist.counts.keys()):
        bar = "#" * max(1, hist.counts[k] * 40 // max(total, 1))
        print(f"  |{k}>: {hist.counts[k]:5d}  {bar}")
    return 0


def _cmd_targets(_args: argparse.Namespace) -> int:
    """List chips by reading the registry next to the spinor source."""
    here = pathlib.Path(__file__).resolve()
    candidates = [
        os.environ.get("SPINOR_REGISTRY_ROOT"),
        str(here.parents[5] / "spinor" / "registry"),
        str(pathlib.Path("spinor/registry").resolve()),
    ]
    root = next((c for c in candidates if c and pathlib.Path(c).exists()),
                None)
    if not root:
        print("spinor_submit: no chip registry found "
              "(set SPINOR_REGISTRY_ROOT)",
              file=sys.stderr)
        return 1
    chips = sorted((pathlib.Path(root) / "chips").glob("*.yaml"))
    for c in chips:
        print(c.stem)
    return 0


def _cmd_providers(_args: argparse.Namespace) -> int:
    rows = [
        ("ibm",      "IBM_QUANTUM_TOKEN",
         "IBM Quantum (qiskit-ibm-runtime SamplerV2; verbatim by Rule 5)"),
        ("aws",      "AWS_DEFAULT_REGION",
         "AWS Braket"),
        ("azure",    "AZURE_QUANTUM_RESOURCE_ID",
         "Azure Quantum"),
        ("local",    "",
         "In-process simulator (spinor C++ LocalProvider). No network."),
        ("qci",      "QCI_API_TOKEN",
         "Quantum Circuits Inc. Aqumen (cassette-only)"),
        ("anyon",    "ANYON_API_TOKEN",
         "Anyon Technologies Yukon (cassette-only)"),
        ("tii",      "QIBOLAB_HOST",
         "TII / Qibolab"),
        ("alicebob", "ALICEBOB_API_KEY",
         "Alice and Bob Boson 4 cat-qubit (cassette-only)"),
    ]
    print(f"{'Provider':<10} {'Env var':<29} Description")
    print(f"{'-'*10:<10} {'-'*29:<29} {'-'*40}")
    for pid, env, desc in rows:
        print(f"{pid:<10} {env:<29} {desc}")
    return 0


def _cmd_version(_args: argparse.Namespace) -> int:
    print(f"spinor_submit {_pkg_version}")
    return 0


def _build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="python -m spinor_submit",
        description="Submission front-end for the Heisenberg quantum stack.",
    )
    sub = p.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("submit", help="Submit a QASM3 file to a provider")
    s.add_argument("--qasm-file", required=True,
                   help="path to a .qasm3 file (use '-' for stdin)")
    s.add_argument("--chip", required=True, help="chip id (e.g. ibm_heron_r2)")
    s.add_argument("--provider", required=True,
                   choices=sorted(SUPPORTED_PROVIDERS))
    s.add_argument("--shots", type=int, default=1024)
    s.add_argument("--mode", choices=["cassette", "live", "local"],
                   default="cassette")
    s.add_argument("--program-name", default=None,
                   help="cassette program name (defaults to qasm-file stem)")
    s.add_argument("--api-key-file", default=None,
                   help="path to one-line file containing the API key")
    s.add_argument("--api-key-stdin", action="store_true",
                   help="read the API key from stdin once")
    s.add_argument("--url", default=None,
                   help="provider REST endpoint override")
    s.add_argument("--region", default=None,
                   help="AWS / Azure region")
    s.add_argument("--instance", default=None,
                   help="IBM hub-group-project / Azure workspace")
    s.add_argument("--extra", action="append", default=[],
                   help="provider-specific k=v (repeatable)")
    s.add_argument("--json", action="store_true",
                   help="emit the histogram as JSON instead of pretty bars")
    s.add_argument("--verbose", action="store_true")
    s.set_defaults(func=_cmd_submit)

    sub.add_parser("targets",
                   help="List chips in the registry").set_defaults(
                       func=_cmd_targets)
    sub.add_parser("providers",
                   help="List supported providers").set_defaults(
                       func=_cmd_providers)
    sub.add_parser("version",
                   help="Print version").set_defaults(
                       func=_cmd_version)
    return p


def main(argv: Sequence[str] | None = None) -> int:
    parser = _build_parser()
    args = parser.parse_args(argv)
    return args.func(args)


if __name__ == "__main__":
    raise SystemExit(main())
