"""Single source of truth for option types, help, environment names and secrets."""
from __future__ import annotations

import json
import math
import re
from dataclasses import dataclass, field
from typing import Any

from .models import QStackError


@dataclass(frozen=True)
class Field:
    description: str
    kind: str = "text"
    secret: bool = False
    choices: tuple = ()
    minimum: float | None = None
    maximum: float | None = None
    positive: bool = False
    aliases: dict[str, tuple[str, ...]] = field(default_factory=dict)
    flags: tuple[str, ...] = ()

    def environment_keys(self, name: str, route: str | None) -> list[str]:
        keys = [f"QSTACK_{route.upper()}_{name.upper()}"] if route else []
        return keys + [f"QSTACK_{name.upper()}", *self.aliases.get(route or "", ()), *self.aliases.get("*", ())]

    def parse(self, name: str, value: Any):
        try:
            if self.kind == "integer":
                if type(value) is not int and not (isinstance(value, str) and re.fullmatch(r"[+-]?[0-9]+", value.strip())):
                    raise ValueError
                value = int(value)
            elif self.kind == "number":
                if isinstance(value, bool) or not isinstance(value, (str, int, float)):
                    raise ValueError
                value = float(value)
                if not math.isfinite(value):
                    raise ValueError
            elif self.kind in {"object", "list"}:
                if isinstance(value, str):
                    value = json.loads(value)
                if not isinstance(value, dict if self.kind == "object" else list):
                    raise ValueError
                json.dumps(value, allow_nan=False)
            elif not isinstance(value, str):
                raise ValueError
        except (ValueError, TypeError, OverflowError) as exc:
            label = {"integer": "an integer", "number": "a finite number", "object": "a JSON object", "list": "a JSON list", "text": "text"}[self.kind]
            raise QStackError(f"{name} must be {label}", "CONFIG_ERROR") from exc
        if self.choices and value not in self.choices:
            raise QStackError(f"{name} must be one of {', '.join(map(str, self.choices))}", "CONFIG_ERROR")
        if self.positive and value <= 0:
            raise QStackError(f"{name} must be positive", "CONFIG_ERROR")
        if self.minimum is not None and value < self.minimum:
            raise QStackError(f"{name} must be at least {self.minimum:g}", "CONFIG_ERROR")
        if self.maximum is not None and value > self.maximum:
            raise QStackError(f"{name} must be at most {self.maximum:g}", "CONFIG_ERROR")
        return value


FIELD_SPECS = {
    "provider": Field("Cloud access route or local simulator"),
    "target": Field("Registry profile or discovered device", flags=("--target", "--chip", "-t")),
    "device": Field("Exact provider device/backend/processor identifier", aliases={"google": ("GOOGLE_PROCESSOR_ID",), "quantinuum": ("QUANTINUUM_DEVICE",)}),
    "mode": Field("Explicit execution mode", choices=("live", "local", "cassette"), aliases={"*": ("SPINOR_SUBMIT_MODE",)}),
    "shots": Field("Number of execution samples", "integer", positive=True),
    "cost_cap_usd": Field("Maximum estimated US-dollar execution cost", "number", minimum=0),
    "registry": Field("Directory containing chip and topology profiles"),
    "python": Field("Python interpreter for compatibility commands"),
    "instance_crn": Field("IBM Cloud instance CRN", aliases={"ibm": ("IBM_INSTANCE_CRN", "IBM_QUANTUM_INSTANCE", "QISKIT_IBM_INSTANCE")}, flags=("--instance-crn", "--instance")),
    "project": Field("Provider project identifier", aliases={"google": ("GOOGLE_CLOUD_PROJECT", "GOOGLE_PROJECT_ID"), "quantinuum": ("QUANTINUUM_PROJECT_ID",), "anyon": ("ANYON_PROJECT_ID",)}),
    "workspace": Field("Provider workspace identifier", aliases={"aqt": ("AQT_WORKSPACE",)}),
    "workspace_resource_id": Field("Full Azure Quantum workspace resource ID", aliases={"azure": ("AZURE_QUANTUM_RESOURCE_ID",)}),
    "resource": Field("AQT resource identifier", aliases={"aqt": ("AQT_RESOURCE",)}),
    "region": Field("Provider service region", aliases={"aws": ("AWS_REGION", "AWS_DEFAULT_REGION"), "quantinuum": ("QUANTINUUM_REGION",)}),
    "url": Field("Provider HTTPS service URL", aliases={"iqm": ("IQM_SERVER_URL",), "oqc": ("OQC_URL",)}),
    "host": Field("Anyon HTTPS service host", aliases={"anyon": ("ANYON_HOST",)}),
    "user": Field("Provider username", aliases={"quantinuum": ("QUANTINUUM_USERNAME",), "anyon": ("ANYON_USER",)}),
    "realm": Field("Anyon account realm", aliases={"anyon": ("ANYON_REALM",)}),
    "quantum_computer": Field("IQM quantum computer selection"),
    "sdk_profile": Field("Provider SDK profile (separate from the qstack profile)", aliases={"aws": ("AWS_PROFILE",), "rigetti": ("QCS_PROFILE_NAME",)}),
    "credentials_file": Field("Exact provider SDK credential file", aliases={"google": ("GOOGLE_APPLICATION_CREDENTIALS",)}),
    "tokens_file": Field("Exact provider SDK token file", aliases={"rigetti": ("QCS_SECRETS_FILE_PATH",)}),
    "sdk_config_file": Field("Exact provider SDK settings file", aliases={"quantinuum": ("NEXUS_CONFIG_FILE",), "rigetti": ("QCS_SETTINGS_FILE_PATH",)}),
    "s3_uri": Field("AWS Braket S3 result location"),
    "client_id": Field("Azure or AQT application client ID", aliases={"azure": ("AZURE_CLIENT_ID",), "aqt": ("AQT_CLIENT_ID",)}),
    "tenant_id": Field("Azure identity tenant ID", aliases={"azure": ("AZURE_TENANT_ID",)}),
    "subscription_id": Field("Azure subscription ID"),
    "resource_group": Field("Azure workspace resource group"),
    "location": Field("Azure workspace location"),
    "platform": Field("Configured Qibolab laboratory platform"),
    "platform_path": Field("Directory containing calibrated Qibolab platforms"),
    "qibolab_platform": Field("Configured Qibolab platform name or directory", aliases={"qibolab": ("QIBOLAB_PLATFORM",)}),
    "timeout": Field("Maximum wait time in seconds", "number", positive=True),
    "poll_interval": Field("Job status polling interval in seconds", "number", positive=True),
    "optimization_level": Field("Owned compiler optimization level", "integer", choices=(0, 1, 2, 3), flags=("-O", "--optimization-level")),
    "seed": Field("Deterministic local simulation seed", "integer", minimum=0, maximum=18446744073709551615),
    "expanded_operation_budget": Field("Maximum operations after bounded-loop expansion", "integer", positive=True),
    "placement_strategy": Field("Owned placement search strategy", choices=("auto", "uniform", "heterogeneous")),
    "placement_max_states": Field("Maximum expanded placement search states", "integer", positive=True),
    "placement_beam_width": Field("Maximum retained placement states per search layer", "integer", positive=True),
    "placement_max_layouts": Field("Maximum alternative initial layouts", "integer", minimum=1, maximum=8),
    "placement_max_swaps": Field("Maximum routing SWAPs per candidate", "integer", minimum=0),
    "verify_max_qubits": Field("Maximum logical qubits for complete offline operators", "integer", minimum=1, maximum=6),
    "verify_max_paths": Field("Maximum offline instrument trajectories", "integer", positive=True, maximum=4096),
    "device_config_name": Field("Google Engine device configuration name"),
    "run_name": Field("Google Engine calibration run name"),
    "snapshot_id": Field("Google Engine calibration snapshot ID"),
    "qibolab_bridge": Field("Optional custom calibrated laboratory bridge, module:factory"),
    "capability_snapshot": Field("Account-provided target capability JSON file"),
    "timing_model": Field("Validated timing/resource model bound to this exact device capability snapshot"),
    "calibration_set_id": Field("IQM calibration set UUID"),
    "qubit_labels": Field("Physical component labels as a JSON list", "list"),
    "input_params": Field("Provider input parameters as a JSON object", "object"),
    "input_data_format": Field("Provider-declared input format"),
    "entry_point": Field("QIR entry point, matching the compiled artifact"),
    "max_cost_hqc": Field("Quantinuum cost limit in HQC units", "number", minimum=0),
    "api_key": Field("Provider API key", secret=True, aliases={"ibm": ("IBM_API_KEY", "IBM_QUANTUM_TOKEN", "QISKIT_IBM_TOKEN"), "ionq": ("IONQ_API_KEY",), "alicebob": ("FELIS_API_KEY", "ALICEBOB_API_KEY")}),
    "token": Field("Provider bearer token", secret=True, aliases={"iqm": ("IQM_TOKEN",), "aqt": ("AQT_TOKEN",)}),
    "password": Field("Provider login password", secret=True, aliases={"quantinuum": ("QUANTINUUM_PASSWORD",)}),
    "access_token": Field("Provider access token", secret=True, aliases={"oqc": ("OQC_ACCESS_TOKEN",), "anyon": ("ANYON_ACCESS_TOKEN", "ANYON_API_TOKEN")}),
    "client_secret": Field("Application client secret", secret=True, aliases={"azure": ("AZURE_CLIENT_SECRET",), "aqt": ("AQT_CLIENT_SECRET",)}),
    "access_key_id": Field("AWS access key ID", secret=True, aliases={"aws": ("AWS_ACCESS_KEY_ID",)}),
    "secret_access_key": Field("AWS secret access key", secret=True, aliases={"aws": ("AWS_SECRET_ACCESS_KEY",)}),
    "session_token": Field("AWS session token", secret=True, aliases={"aws": ("AWS_SESSION_TOKEN",)}),
    "connection_string": Field("Enabled Azure workspace connection string", secret=True, aliases={"azure": ("AZURE_QUANTUM_CONNECTION_STRING",)}),
}

FIELDS = frozenset(FIELD_SPECS)
SECRET_FIELDS = frozenset(name for name, spec in FIELD_SPECS.items() if spec.secret)
