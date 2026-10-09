"""qstack: compile, configure authentication, and resume provider jobs."""
from __future__ import annotations

import argparse
import json
import os
import sys
import tomllib
from pathlib import Path

from . import __version__
from .artifacts import load_artifact
from .config import FIELD_SPECS, SECRET_FIELDS, ResolvedConfig, config_path, redact_data, resolve_config, state_path
from .jobs import load_job, read_with_retry, save_job
from .models import QStackError, SubmissionOptions
from .registry import cache_targets, get_target, profiles


def parser() -> argparse.ArgumentParser:
    from .providers import ADAPTERS, UNAVAILABLE
    shared = argparse.ArgumentParser(add_help=False)
    for name, spec in sorted(FIELD_SPECS.items()):
        flags = spec.flags or ("--" + name.replace("_", "-"),)
        aliases = [f"{route}: {', '.join(names)}" for route, names in spec.aliases.items()]
        help_text = (spec.description + f". Environment: QSTACK_<ROUTE>_{name.upper()}" +
                     ("; " + "; ".join(aliases) if aliases else ""))
        kwargs = {"dest": name, "default": argparse.SUPPRESS, "help": help_text}
        if name == "provider":
            kwargs["choices"] = [*ADAPTERS, *UNAVAILABLE, "local"]
        elif spec.choices:
            # Values are parsed by the same typed resolver for CLI, env and TOML.
            kwargs["metavar"] = "{" + ",".join(map(str, spec.choices)) + "}"
        shared.add_argument(*flags, **kwargs)
        if spec.secret:
            flag = "--" + name.replace("_", "-")
            shared.add_argument(flag + "-file", default=argparse.SUPPRESS, metavar="PATH",
                                help=f"Read {name} from this exact UTF-8 secret file")
            shared.add_argument(flag + "-stdin", action="store_true", default=argparse.SUPPRESS,
                                help=f"Read {name} from stdin (one credential per command)")
    shared.add_argument("--config", "--config-file", default=argparse.SUPPRESS)
    shared.add_argument("--profile", default=argparse.SUPPRESS)
    shared.add_argument("--env-file", "--env-path", action="append", default=argparse.SUPPRESS)
    shared.add_argument("--no-env-file", action="store_true", default=argparse.SUPPRESS)
    shared.add_argument("--json", action="store_true", default=argparse.SUPPRESS)
    shared.add_argument("--verbose", "-v", action="store_true", default=argparse.SUPPRESS)
    shared.add_argument("--quiet", "-q", action="store_true", default=argparse.SUPPRESS)
    class SafeParser(argparse.ArgumentParser):
        def error(self, message):
            # argparse's default diagnostics repeat unknown argument values, which
            # can contain mistyped credential flags and secrets.
            self.exit(2, "Invalid command-line arguments; use --help for accepted options.\n")
    root = SafeParser(prog="qstack", description="Owned quantum compilation and explicit provider execution", parents=[shared])
    root.add_argument("--version", action="version", version=__version__)
    commands = root.add_subparsers(dest="command", required=True)
    commands.add_parser("version")
    commands.add_parser("providers", parents=[shared])
    targets = commands.add_parser("targets", parents=[shared]).add_subparsers(dest="action", required=True)
    for name in ("list", "refresh"):
        targets.add_parser(name, parents=[shared])
    auth = commands.add_parser("auth", parents=[shared]).add_subparsers(dest="action", required=True)
    for name in ("configure", "login", "check", "logout"):
        sub = auth.add_parser(name, parents=[shared])
        if name == "configure":
            sub.add_argument("--store-key", action="store_true", help="Store supplied secrets in the operating-system keyring")
    config = commands.add_parser("config", parents=[shared]).add_subparsers(dest="action", required=True)
    sub = config.add_parser("show", parents=[shared])
    sub.add_argument("--resolved", action="store_true")
    sub = commands.add_parser("verify", parents=[shared], help="Offline independent operator/instrument verification")
    sub.add_argument("input", help="Compiled artifact directory")
    for name in ("compile", "estimate", "run", "submit"):
        sub = commands.add_parser(name, parents=[shared])
        sub.add_argument("input", nargs="?")
        sub.add_argument("--qasm-file", default=argparse.SUPPRESS)
        sub.add_argument("--language", choices=["photon", "phonon", "spinor", "qasm"])
        sub.add_argument("--format", "--emit", "-f")
        sub.add_argument("--out", "-o")
        sub.add_argument("--name", "--program-name", default="qstack")
        sub.add_argument("--wait", action="store_true")
        sub.add_argument("--dry-run", action="store_true")
        sub.add_argument("--verbatim", action="store_true", default=True)
        sub.add_argument("--manifest", action="store_true")
    jobs = commands.add_parser("jobs", parents=[shared]).add_subparsers(dest="action", required=True)
    for name in ("status", "results", "cancel"):
        sub = jobs.add_parser(name, parents=[shared])
        sub.add_argument("reference")
    return root


def _resolve(args: dict, route: str | None = None) -> ResolvedConfig:
    args = dict(args)
    # Learn the route before reading stdin or any provider-specific variables.
    # These local reads perform no authentication and cannot consume credentials.
    if route is None and args.get("command") in {"submit", "estimate"}:
        source = args.get("input") or args.get("qasm_file")
        if source and Path(source).is_dir():
            route = load_artifact(source).route
    if route is None and args.get("command") == "jobs" and args.get("reference"):
        route = load_job(args["reference"])[0].route
    if route and args.get("provider") and args["provider"] != route:
        raise QStackError("Provider argument conflicts with the artifact or job receipt route")
    if args.get("command") == "auth" and args.get("action") == "configure":
        args["allow_new_profile"] = True
    if route or args.get("provider"):
        return resolve_config(route or args["provider"], args)
    secret_inputs = {field + suffix for field in SECRET_FIELDS for suffix in ("_file", "_stdin")}
    bootstrap = {k: v for k, v in args.items() if k not in secret_inputs}
    initial = resolve_config(route, bootstrap)
    route = route or initial.values.get("provider")
    if not route and initial.values.get("target"):
        route = get_target(initial.values["target"], config=initial.values)["provider"]
    return resolve_config(route, args)


def _save_profile(args: dict, resolved: ResolvedConfig) -> dict:
    path = resolved.profile_path or config_path()
    data = tomllib.loads(path.read_text(encoding="utf-8")) if path.exists() else {}
    profile = resolved.profile
    values = {k: v for k, v in resolved.values.items() if k not in SECRET_FIELDS}
    previous_store = data.get("profiles", {}).get(profile, {}).get("secret_store", {})
    if previous_store:
        values["secret_store"] = dict(previous_store)
    if args.get("store_key"):
        try:
            import keyring
        except ImportError as exc:
            raise QStackError("Install the keyring extra for secure credential storage", "MISSING_DEPENDENCY") from exc
        refs = dict(previous_store)
        for field in SECRET_FIELDS:
            if field in resolved.values:
                account = f"{profile}:{resolved.values['provider']}:{field}"
                keyring.set_password("qstack", account, str(resolved.values[field]))
                refs[field] = account
        values["secret_store"] = refs
    data.setdefault("profiles", {})[profile] = values
    # This writer supports our own scalar profiles and nested keyring references.
    lines = []
    def toml_value(value):
        if isinstance(value, dict):
            return "{ " + ", ".join(json.dumps(str(k)) + " = " + toml_value(v) for k, v in value.items()) + " }"
        if isinstance(value, (list, tuple)):
            return "[" + ", ".join(toml_value(v) for v in value) + "]"
        if value is None:
            raise QStackError("TOML profiles cannot store null; omit that optional value", "CONFIG_ERROR")
        return json.dumps(value, allow_nan=False)
    def write_table(keys, table):
        lines.append("[" + ".".join(json.dumps(str(k)) for k in keys) + "]")
        for key, value in table.items():
            if not isinstance(value, dict):
                lines.append(json.dumps(key) + " = " + toml_value(value))
        lines.append("")
        for key, value in table.items():
            if isinstance(value, dict):
                write_table([*keys, key], value)
    for key, value in data.items():
        if isinstance(value, dict):
            write_table([key], value)
    path.parent.mkdir(parents=True, exist_ok=True)
    temporary = path.with_suffix(f".{os.getpid()}.tmp")
    temporary.write_text("\n".join(lines), encoding="utf-8")
    temporary.replace(path)
    return {"profile": profile, "config": str(path), "secrets_stored": bool(args.get("store_key"))}


def execute(args: dict, resolved: ResolvedConfig):
    from .providers import ADAPTERS, UNAVAILABLE, get_adapter
    from .service import compile_file, submit_artifact
    config = resolved.values
    command = args["command"]
    if command == "verify":
        from .verification import verify_artifact
        return verify_artifact(load_artifact(args["input"]), max_qubits=config.get("verify_max_qubits", 4),
                               max_paths=config.get("verify_max_paths", 256))
    if command == "version":
        return {"version": __version__, "component": "qstack / spinor_submit"}
    if command == "providers":
        return [{"route": route, "capabilities": get_adapter(route).capabilities,
                 "status": "unavailable" if route in UNAVAILABLE else "implemented"}
                for route in [*ADAPTERS, *UNAVAILABLE]] + [{"route": "local", "status": "C++ simulator"}]
    if command == "config":
        return resolved.redacted()
    if command == "targets":
        if args["action"] == "list":
            cached = [json.loads(p.read_bytes()) for p in (state_path() / "targets").glob("*/*.json")]
            return {"profiles": profiles(config), "discovered": cached}
        if not config.get("provider"):
            raise QStackError("targets refresh requires --provider or a configured profile")
        records = read_with_retry(get_adapter(config["provider"], config).discover)
        return cache_targets(config["provider"], redact_data(records, config))
    if command == "auth":
        if not config.get("provider"):
            raise QStackError("Authentication commands require --provider or a configured profile")
        if args["action"] == "configure":
            return _save_profile(args, resolved)
        if args["action"] == "logout":
            try:
                import keyring
            except ImportError as exc:
                raise QStackError("Install the keyring extra to remove qstack credentials", "MISSING_DEPENDENCY") from exc
            for field in SECRET_FIELDS:
                account = f"{resolved.profile}:{config['provider']}:{field}"
                if keyring.get_password("qstack", account) is not None:
                    keyring.delete_password("qstack", account)
            return {"removed": "qstack keyring credentials", "sdk_sessions": "Use the provider SDK logout command for shared sessions"}
        adapter = get_adapter(config["provider"], config)
        return adapter.login() if args["action"] == "login" else read_with_retry(adapter.check_auth)
    if command == "jobs":
        receipt, cached = load_job(args["reference"])
        if receipt.mode != "live":
            if args["action"] == "cancel":
                raise QStackError("Local/cassette job has already completed", "JOB_ALREADY_TERMINAL")
            return cached if args["action"] == "results" else {"status": "completed", "mode": receipt.mode}
        current = resolved.values
        context = {**receipt.metadata.get("context", {}), **current}
        adapter = get_adapter(receipt.route, context)
        if not adapter.capabilities.get(args["action"], False):
            raise QStackError(f"{receipt.route} does not expose {args['action']}", "UNSUPPORTED_CAPABILITY")
        if args["action"] == "status":
            return read_with_retry(lambda: adapter.status(receipt))
        if args["action"] == "cancel":
            return adapter.cancel(receipt)
        result = read_with_retry(lambda: adapter.results(receipt))
        save_job(receipt, result, context)
        return result
    source = args.get("input") or args.get("qasm_file")
    if command == "compile" and not args.get("out"):
        raise QStackError("compile requires --out DIRECTORY to save the complete artifact")
    if not source:
        raise QStackError("An input source or artifact directory is required")
    if command in {"submit", "estimate"} and Path(source).is_dir():
        artifact = load_artifact(source)
        if config.get("provider") != artifact.route:
            raise QStackError("Provider argument conflicts with the artifact route")
    else:
        target = config.get("target") or config.get("device")
        if not target:
            raise QStackError("Select --target or configure a target/device")
        artifact = compile_file(source, target=target, config=config, output=args.get("out"),
                                language=args.get("language"), format=args.get("format"),
                                optimization_level=config.get("optimization_level", 2))
    if command == "compile":
        if not args.get("out"):
            raise QStackError("compile requires --out DIRECTORY to save the complete artifact")
        return {"artifact": artifact.path, "target": artifact.target, "format": artifact.format,
                "statistics": artifact.manifest["statistics"]}
    if command == "estimate":
        adapter = get_adapter(artifact.route, config)
        options = SubmissionOptions(shots=config.get("shots", 1024), mode="live")
        estimate = read_with_retry(lambda: adapter.estimate(artifact, options)) if hasattr(adapter, "estimate") else None
        return {"statistics": artifact.manifest["statistics"], "pricing": estimate,
                "estimated_usd": estimate.get("usd") if isinstance(estimate, dict) else None}
    mode = config.get("mode")
    if mode is None:
        raise QStackError("Select --mode live|local|cassette or set a mode in your profile", "MODE_REQUIRED")
    options = SubmissionOptions(shots=config.get("shots", 1024), name=args.get("name", "qstack"),
                                mode=mode, cost_cap_usd=config.get("cost_cap_usd"))
    return submit_artifact(artifact, options, config, wait=args.get("wait", False), dry_run=args.get("dry_run", False))


def main(argv: list[str] | None = None) -> int:
    resolved = ResolvedConfig({}, {})
    try:
        if argv is None and os.environ.get("QSTACK_FORWARDED_ARGS"):
            argv = json.loads(os.environ.pop("QSTACK_FORWARDED_ARGS"))
        args = vars(parser().parse_args(argv))
        resolved = _resolve(args)
        result = execute(args, resolved)
        if hasattr(result, "to_dict"):
            result = result.to_dict()
        if not args.get("quiet"):
            # Keep source labels for config show while redacting values in all
            # provider responses, including successful responses that echo tokens.
            if args.get("command") != "config":
                result = redact_data(result, resolved.values)
            print(resolved.redact_text(json.dumps(result, indent=None if args.get("json") else 2, allow_nan=False)))
        if args.get("command") == "verify":
            return {"passed": 0, "failed": 1, "not_checked": 3}[result["status"]]
        if isinstance(result, dict) and result.get("metadata", {}).get("application_status") == "loop_exhausted":
            return 4
        if isinstance(result, dict) and result.get("metadata", {}).get("application_status") == "not_checked":
            return 5
        return 0
    except QStackError as exc:
        error = {"error": exc.code, "message": resolved.redact_text(str(exc))}
        if exc.details:
            error["details"] = exc.details
        print(resolved.redact_text(json.dumps(redact_data(error, resolved.values))), file=sys.stderr)
        return 2
    except (OSError, ValueError, TypeError, KeyError) as exc:
        print(json.dumps({"error": "INVALID_REQUEST", "message": f"Invalid input or provider response ({type(exc).__name__}); check configuration, source and artifact format"}), file=sys.stderr)
        return 2
    except Exception as exc:
        # SDK exceptions may embed request headers or credentials in their text.
        print(json.dumps({"error": "PROVIDER_ERROR", "message": f"Operation failed ({type(exc).__name__}); run auth check and inspect provider job status before retrying submission"}), file=sys.stderr)
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
