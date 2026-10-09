"""One configuration resolver for every command and legacy entry point."""
from __future__ import annotations

import io
import os
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping

from dotenv.parser import parse_stream
from platformdirs import user_config_path, user_state_path

from .models import QStackError

from .fields import FIELD_SPECS, FIELDS, SECRET_FIELDS


def redact_data(value: Any, credentials: Mapping[str, Any] | None = None):
    """Remove credential fields and known secret values, including provider echoes."""
    import json
    from urllib.parse import quote
    secret_keys = {key.replace("_", "") for key in SECRET_FIELDS} | {
        "authorization", "credentials", "headers", "authenticationtoken",
        # Provider-managed OAuth values can appear in responses even when no
        # application credential was configured (SDK cache/managed identity).
        "refreshtoken", "idtoken", "bearertoken", "apitoken", "authtoken",
        "oauthtoken", "clientassertion"}
    def is_secret(key):
        return str(key).lower().replace("_", "").replace("-", "") in secret_keys
    secrets = {str(item) for key, item in (credentials or {}).items() if is_secret(key) and isinstance(item, str) and item}
    def collect(item):
        if isinstance(item, dict):
            for key, nested in item.items():
                if is_secret(key) and isinstance(nested, str) and nested:
                    secrets.add(nested)
                elif isinstance(nested, (dict, list, tuple)):
                    collect(nested)
        elif isinstance(item, (list, tuple)):
            for nested in item:
                collect(nested)
    collect(value)
    variants = set()
    for secret in secrets:
        variants.update((secret, repr(secret)[1:-1], json.dumps(secret)[1:-1], quote(secret, safe="")))
    ordered = sorted(variants, key=len, reverse=True)
    def clean(item):
        if isinstance(item, dict):
            return {clean(key): clean(nested) for key, nested in item.items() if not is_secret(key)}
        if isinstance(item, (list, tuple)):
            return [clean(nested) for nested in item]
        if isinstance(item, str):
            for secret in ordered:
                item = item.replace(secret, "<redacted>")
        return item
    return clean(value)


def config_path() -> Path:
    return Path(os.environ.get("QSTACK_CONFIG", user_config_path("qstack") / "config.toml"))


def state_path() -> Path:
    return Path(os.environ.get("QSTACK_STATE_DIR", user_state_path("qstack")))


def dotenv_values(path: Path) -> dict[str, str]:
    try:
        stream = io.StringIO(path.read_text(encoding="utf-8-sig"))
    except (OSError, UnicodeError) as exc:
        raise QStackError(f"Cannot read env file: {path}", "CONFIG_ERROR") from exc
    result = {}
    for binding in parse_stream(stream):
        if binding.error or (binding.key is not None and binding.value is None):
            raise QStackError(f"Invalid env assignment in {path} at line {binding.original.line}", "CONFIG_ERROR")
        if binding.key is not None:
            result[binding.key] = binding.value
    return result


@dataclass
class ResolvedConfig:
    values: dict[str, Any]
    sources: dict[str, str]
    profile: str = "default"
    profile_path: Path | None = None

    def redacted(self) -> dict[str, Any]:
        return {k: {"value": "<redacted>" if k in SECRET_FIELDS else v, "source": self.sources[k]}
                for k, v in self.values.items()}

    def redact_text(self, text: str) -> str:
        import json
        from urllib.parse import quote
        for key in SECRET_FIELDS:
            value = self.values.get(key)
            if value:
                variants = {str(value), repr(str(value))[1:-1], json.dumps(str(value))[1:-1], quote(str(value), safe="")}
                for variant in sorted(variants, key=len, reverse=True):
                    text = text.replace(variant, "<redacted>")
        return text


def resolve_config(route: str | None, cli: Mapping[str, Any] | None = None, *,
                   environ: Mapping[str, str] | None = None, cwd: Path | None = None,
                   stdin=None) -> ResolvedConfig:
    cli = dict(cli or {})
    env = dict(os.environ if environ is None else environ)
    cwd = cwd or Path.cwd()
    explicit_files = cli.get("env_file") or []
    if isinstance(explicit_files, (str, Path)):
        explicit_files = [explicit_files]
    if explicit_files and cli.get("no_env_file"):
        raise QStackError("--env-file and --no-env-file cannot be combined", "CONFIG_ERROR")
    files = [] if cli.get("no_env_file") else [Path(p) for p in explicit_files]
    if not files and not cli.get("no_env_file") and (cwd / ".env").is_file():
        files = [cwd / ".env"]
    dotenv: dict[str, str] = {}
    dotenv_layers = []
    for file in files:
        file = file if file.is_absolute() else cwd / file
        values = dotenv_values(file)
        dotenv.update(values)
        dotenv_layers.append((values, f"env-file:{file}"))
    cfg_path = Path(cli.get("config") or env.get("QSTACK_CONFIG") or user_config_path("qstack") / "config.toml")
    data = {}
    if cfg_path.exists():
        try:
            data = tomllib.loads(cfg_path.read_text(encoding="utf-8-sig"))
        except (OSError, tomllib.TOMLDecodeError) as exc:
            raise QStackError(f"Invalid TOML config: {cfg_path}", "CONFIG_ERROR") from exc
    elif (cli.get("config") or env.get("QSTACK_CONFIG")) and not cli.get("allow_new_profile"):
        raise QStackError(f"Config file not found: {cfg_path}", "CONFIG_ERROR")
    profile = cli.get("profile") or env.get("QSTACK_PROFILE") or dotenv.get("QSTACK_PROFILE") or "default"
    profiles = data.get("profiles", {})
    if profile != "default" and profile not in profiles and not cli.get("allow_new_profile"):
        raise QStackError(f"Unknown profile: {profile}", "CONFIG_ERROR")
    defaults = dict(data.get("defaults", {}))
    defaults.update(profiles.get(profile, {}))
    if any(key in SECRET_FIELDS for key in defaults):
        raise QStackError("TOML profiles must contain no secrets; use env, credential files or secure storage", "CONFIG_ERROR")
    route = route or cli.get("provider") or env.get("QSTACK_PROVIDER") or dotenv.get("QSTACK_PROVIDER") or defaults.get("provider")
    values, sources = {}, {}
    stored = defaults.pop("secret_store", {})
    if stored:
        try:
            import keyring
        except ImportError as exc:
            raise QStackError("Install the keyring extra to load this profile's stored credentials", "MISSING_DEPENDENCY") from exc
        for field, account in stored.items():
            if field not in SECRET_FIELDS:
                raise QStackError("Invalid secure credential field in profile", "CONFIG_ERROR")
            secret = keyring.get_password("qstack", account)
            if secret is not None:
                values[field], sources[field] = secret, "keyring"
    for field in sorted(FIELDS):
        if field in defaults:
            values[field], sources[field] = defaults[field], f"profile:{profile}"
        keys = FIELD_SPECS[field].environment_keys(field, route)
        for mapping, label in (*dotenv_layers, (env, "environment")):
            # Canonical names win within one source. Resolve each file separately
            # so a later file wins even when it uses a documented SDK alias.
            match = next((k for k in keys if k in mapping), None)
            if match is not None:
                values[field] = mapping[match]
                sources[field] = f"environment:{match}" if label == "environment" else label
        if cli.get(field) is not None:
            values[field], sources[field] = cli[field], "argument"
    if route:
        values["provider"] = route
        sources.setdefault("provider", "target")
    stdin_fields = [field for field in SECRET_FIELDS if cli.get(field + "_stdin")]
    if len(stdin_fields) > 1:
        raise QStackError("Only one credential can read stdin in a command; use secret files for additional credentials", "CONFIG_ERROR")
    for field in sorted(SECRET_FIELDS):
        file_option, stdin_option = field + "_file", field + "_stdin"
        supplied = [bool(cli.get(file_option)), bool(cli.get(stdin_option)), cli.get(field) is not None]
        flag = field.replace("_", "-")
        if sum(supplied) > 1:
            raise QStackError(f"Choose exactly one of --{flag}, --{flag}-file or --{flag}-stdin", "CONFIG_ERROR")
        if not (cli.get(file_option) or cli.get(stdin_option)):
            continue
        try:
            secret_path = Path(cli[file_option]) if cli.get(file_option) else None
            if secret_path is not None and not secret_path.is_absolute():
                secret_path = cwd / secret_path
            secret = (secret_path.read_text(encoding="utf-8-sig") if secret_path is not None
                      else (stdin or sys.stdin).read()).strip()
        except (OSError, UnicodeError) as exc:
            raise QStackError(f"Cannot read {flag} input", "CONFIG_ERROR") from exc
        if not secret:
            raise QStackError(f"{flag} input is empty", "CONFIG_ERROR")
        # Compatibility with the former Spinor IBM JSON key file.
        if route == "ibm" and field == "api_key" and secret.startswith("{"):
            import json
            try:
                key_data = json.loads(secret)
                secret = key_data["token"]
                if not cli.get("instance_crn") and key_data.get("instance"):
                    values["instance_crn"], sources["instance_crn"] = key_data["instance"], "credential-file"
            except (ValueError, KeyError, TypeError) as exc:
                raise QStackError("Invalid IBM credential file", "CONFIG_ERROR") from exc
        values[field], sources[field] = secret, "credential-file" if secret_path is not None else "stdin"
    for field, value in list(values.items()):
        values[field] = FIELD_SPECS[field].parse(field, value)
    return ResolvedConfig(values, sources, profile, cfg_path)
