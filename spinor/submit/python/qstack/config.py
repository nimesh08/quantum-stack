"""One configuration resolver for every command and legacy entry point."""
from __future__ import annotations

import io
import math
import os
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Mapping

from dotenv.parser import parse_stream
from platformdirs import user_config_path, user_state_path

from .models import QStackError

SECRET_FIELDS = frozenset({"api_key", "token", "password", "access_token", "client_secret",
                           "access_key_id", "secret_access_key", "session_token", "connection_string"})
FIELDS = {
    "provider", "target", "device", "mode", "shots", "cost_cap_usd", "registry", "python",
    "instance_crn", "project", "workspace", "workspace_resource_id", "resource", "region",
    "url", "host", "user", "realm", "quantum_computer", "sdk_profile", "credentials_file",
    "tokens_file", "sdk_config_file", "s3_uri", "client_id", "tenant_id", "subscription_id",
    "resource_group", "location", "platform", "platform_path", "timeout", "poll_interval",
    "optimization_level", "seed", "device_config_name", "run_name", "snapshot_id",
    "qibolab_bridge", "capability_snapshot", "calibration_set_id", "qubit_labels", "input_params", "input_data_format", "entry_point", "max_cost_hqc", *SECRET_FIELDS,
}
ALIASES: dict[str, dict[str, tuple[str, ...]]] = {
    "ibm": {"api_key": ("IBM_API_KEY", "IBM_QUANTUM_TOKEN", "QISKIT_IBM_TOKEN"),
            "instance_crn": ("IBM_INSTANCE_CRN", "IBM_QUANTUM_INSTANCE", "QISKIT_IBM_INSTANCE")},
    "aws": {"access_key_id": ("AWS_ACCESS_KEY_ID",), "secret_access_key": ("AWS_SECRET_ACCESS_KEY",),
            "session_token": ("AWS_SESSION_TOKEN",), "region": ("AWS_REGION", "AWS_DEFAULT_REGION"),
            "sdk_profile": ("AWS_PROFILE",)},
    "google": {"credentials_file": ("GOOGLE_APPLICATION_CREDENTIALS",),
               "project": ("GOOGLE_CLOUD_PROJECT", "GOOGLE_PROJECT_ID"), "device": ("GOOGLE_PROCESSOR_ID",)},
    "azure": {"tenant_id": ("AZURE_TENANT_ID",), "client_id": ("AZURE_CLIENT_ID",),
              "client_secret": ("AZURE_CLIENT_SECRET",), "connection_string": ("AZURE_QUANTUM_CONNECTION_STRING",),
              "workspace_resource_id": ("AZURE_QUANTUM_RESOURCE_ID",)},
    "quantinuum": {"sdk_config_file": ("NEXUS_CONFIG_FILE",), "project": ("QUANTINUUM_PROJECT_ID",),
                   "device": ("QUANTINUUM_DEVICE",), "region": ("QUANTINUUM_REGION",),
                   "user": ("QUANTINUUM_USERNAME",), "password": ("QUANTINUUM_PASSWORD",)},
    "ionq": {"api_key": ("IONQ_API_KEY",)},
    "rigetti": {"sdk_profile": ("QCS_PROFILE_NAME",), "sdk_config_file": ("QCS_SETTINGS_FILE_PATH",),
                "tokens_file": ("QCS_SECRETS_FILE_PATH",)},
    "iqm": {"token": ("IQM_TOKEN",), "url": ("IQM_SERVER_URL",)},
    "oqc": {"access_token": ("OQC_ACCESS_TOKEN",), "url": ("OQC_URL",)},
    "aqt": {"token": ("AQT_TOKEN",), "workspace": ("AQT_WORKSPACE",), "resource": ("AQT_RESOURCE",),
            "client_id": ("AQT_CLIENT_ID",), "client_secret": ("AQT_CLIENT_SECRET",)},
    "anyon": {"access_token": ("ANYON_ACCESS_TOKEN", "ANYON_API_TOKEN"), "host": ("ANYON_HOST",),
              "user": ("ANYON_USER",), "project": ("ANYON_PROJECT_ID",), "realm": ("ANYON_REALM",)},
    "alicebob": {"api_key": ("FELIS_API_KEY", "ALICEBOB_API_KEY")},
}


def config_path() -> Path:
    return Path(os.environ.get("QSTACK_CONFIG", user_config_path("qstack") / "config.toml"))


def state_path() -> Path:
    return Path(os.environ.get("QSTACK_STATE_DIR", user_state_path("qstack")))


def dotenv_values(path: Path) -> dict[str, str]:
    try:
        stream = io.StringIO(path.read_text(encoding="utf-8-sig"))
    except OSError as exc:
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
    dot_sources = {}
    for file in files:
        file = file if file.is_absolute() else cwd / file
        values = dotenv_values(file)
        dotenv.update(values)
        dot_sources.update({key: f"env-file:{file}" for key in values})
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
        keys = ([f"QSTACK_{route.upper()}_{field.upper()}"] if route else [])
        keys += [f"QSTACK_{field.upper()}"]
        keys += list(ALIASES.get(route or "", {}).get(field, ()))
        if field == "mode":
            keys.append("SPINOR_SUBMIT_MODE")
        for mapping, label in ((dotenv, "env-file"), (env, "environment")):
            # Canonical names take priority over legacy aliases within one source.
            match = next((k for k in keys if k in mapping), None)
            if match is not None:
                values[field] = mapping[match]
                sources[field] = dot_sources[match] if label == "env-file" else f"environment:{match}"
        if cli.get(field) is not None:
            values[field], sources[field] = cli[field], "argument"
    if route:
        values["provider"] = route
        sources.setdefault("provider", "target")
    secret_inputs = [bool(cli.get("api_key_file")), bool(cli.get("api_key_stdin")), cli.get("api_key") is not None]
    if sum(secret_inputs) > 1:
        raise QStackError("Choose exactly one of --api-key, --api-key-file or --api-key-stdin", "CONFIG_ERROR")
    if cli.get("api_key_file") or cli.get("api_key_stdin"):
        try:
            secret = (Path(cli["api_key_file"]).read_text(encoding="utf-8-sig")
                      if cli.get("api_key_file") else (stdin or sys.stdin).read()).strip()
        except OSError as exc:
            raise QStackError("Cannot read API key file", "CONFIG_ERROR") from exc
        if not secret:
            raise QStackError("API key input is empty", "CONFIG_ERROR")
        # Compatibility with the former Spinor IBM JSON key file.
        if route == "ibm" and secret.startswith("{"):
            import json
            try:
                key_data = json.loads(secret)
                secret = key_data["token"]
                if not cli.get("instance_crn") and key_data.get("instance"):
                    values["instance_crn"], sources["instance_crn"] = key_data["instance"], "credential-file"
            except (ValueError, KeyError, TypeError) as exc:
                raise QStackError("Invalid IBM credential file", "CONFIG_ERROR") from exc
        values["api_key"], sources["api_key"] = secret, "credential-file" if cli.get("api_key_file") else "stdin"
    for field in ("shots", "seed", "optimization_level"):
        if field in values:
            try:
                if isinstance(values[field], bool):
                    raise ValueError("boolean is not an integer option")
                values[field] = int(values[field])
            except (ValueError, TypeError) as exc:
                raise QStackError(f"{field} must be an integer", "CONFIG_ERROR") from exc
    for field in ("qubit_labels", "input_params"):
        if isinstance(values.get(field), str):
            import json
            try:
                values[field] = json.loads(values[field])
            except ValueError as exc:
                raise QStackError(f"{field} must contain JSON", "CONFIG_ERROR") from exc
    for field in ("cost_cap_usd", "timeout", "poll_interval"):
        if field in values:
            try:
                values[field] = float(values[field])
                if not math.isfinite(values[field]):
                    raise ValueError("value is not finite")
            except (ValueError, TypeError) as exc:
                raise QStackError(f"{field} must be numeric", "CONFIG_ERROR") from exc
    if "shots" in values and values["shots"] <= 0:
        raise QStackError("shots must be positive", "CONFIG_ERROR")
    for field in ("timeout", "poll_interval"):
        if field in values and values[field] <= 0:
            raise QStackError(f"{field} must be positive", "CONFIG_ERROR")
    if values.get("cost_cap_usd", 0) < 0:
        raise QStackError("cost_cap_usd must be nonnegative", "CONFIG_ERROR")
    if values.get("mode") not in {None, "live", "local", "cassette"}:
        raise QStackError("mode must be live, local or cassette", "CONFIG_ERROR")
    if "optimization_level" in values and values["optimization_level"] not in range(4):
        raise QStackError("optimization level must be 0, 1, 2 or 3", "CONFIG_ERROR")
    return ResolvedConfig(values, sources, profile, cfg_path)
