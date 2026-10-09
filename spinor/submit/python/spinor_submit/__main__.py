"""Compatibility entry point with the same configuration resolver as qstack."""
from __future__ import annotations
import json
import sys
from pathlib import Path
from qstack.cli import parser, main as qstack_main
from qstack.config import resolve_config
from qstack.models import QStackError
from . import submit

def main(argv=None):
    argv = list(sys.argv[1:] if argv is None else argv)
    if argv and argv[0] == "targets":
        return qstack_main(["targets", "list", *argv[1:]])
    if not argv or argv[0] != "submit":
        return qstack_main(argv)
    args = vars(parser().parse_args(argv))
    route = args.get("provider")
    resolved = resolve_config(route, args)
    try:
        source = args.get("input") or args.get("qasm_file")
        if not source:
            raise QStackError("--qasm-file or a source file is required")
        hist = submit(Path(source).read_text(encoding="utf-8"), resolved.values.get("target", "generic"),
                      provider=route, shots=resolved.values.get("shots", 1024),
                      program_name=args.get("name", "default"), mode=resolved.values.get("mode"),
                      config=resolved.values)
        if args.get("json"):
            print(json.dumps({"counts": hist.counts, "shots": hist.shots, "total": sum(hist.counts.values()), "mode": hist.mode}))
        else:
            print(f"mode={hist.mode} shots={hist.shots}\nhistogram:")
            for bits, count in sorted(hist.counts.items()):
                print(f"  |{bits}>: {count}")
        return 0
    except (OSError, ValueError, QStackError) as exc:
        print(resolved.redact_text(str(exc)), file=sys.stderr)
        return 2

if __name__ == "__main__":
    raise SystemExit(main())
