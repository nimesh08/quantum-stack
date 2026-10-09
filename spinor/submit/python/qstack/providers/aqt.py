"""Arnica v1 native circuit contract, verified against AQT connector 0.4/provider 2.0."""
from __future__ import annotations

from collections import Counter
from math import pi
from pathlib import Path
import tomllib
from qstack.models import QStackError
from .base import RestAdapter, optional, segment
from .native import instructions


class AQTAdapter(RestAdapter):
    route = "aqt"
    base_url = "https://arnica.aqt.eu/api"
    capabilities = {**RestAdapter.capabilities, "login": True, "cancel": False}

    def app(self):
        if not hasattr(self, "_app"):
            sdk = optional("aqt_connector", "aqt")
            if self.config.get("credentials_file"):
                path = Path(self.config["credentials_file"]).expanduser()
                try:
                    data = tomllib.loads(path.read_text(encoding="utf-8-sig"))
                    file_config = data.get("default", data)
                    if not isinstance(file_config, dict):
                        raise ValueError("invalid config table")
                    for key in ("arnica_url", "client_id", "client_secret"):
                        if key in file_config and not isinstance(file_config[key], str):
                            raise ValueError("invalid config field")
                    if "store_access_token" in file_config and type(file_config["store_access_token"]) is not bool:
                        raise ValueError("invalid persistence flag")
                except (OSError, ValueError) as exc:
                    raise QStackError("Cannot read a valid AQT TOML credentials file", "CONFIG_ERROR") from exc
                # Pinned connector 0.4 accepts an app directory and otherwise
                # reads only app_dir/config. Substitute exactly the requested
                # parsed file in its loader hook; never copy secret material or
                # read a sibling config. The normal SDK env precedence remains.
                class ExplicitFileConfig(sdk.ArnicaConfig):
                    def _add_file_config(self, config, config_filepath):
                        return {**config, **file_config}
                config = ExplicitFileConfig(path.parent)
            else:
                config = sdk.ArnicaConfig()
            for source, attr in (("url", "arnica_url"), ("client_id", "client_id"), ("client_secret", "client_secret")):
                if self.config.get(source):
                    setattr(config, attr, self.config[source])
            self._app = sdk.ArnicaApp(config)
        return self._app

    def headers(self):
        token = self.config.get("api_key") or self.config.get("token") or optional("aqt_connector", "aqt").get_access_token(self.app())
        if not token:
            raise QStackError("AQT requires a Bearer token or Arnica login", "MISSING_CREDENTIALS")
        return {"Authorization": "Bearer " + token}

    def login(self):
        optional("aqt_connector", "aqt").log_in(self.app())
        return {"route": self.route, "authenticated": True, "method": "Arnica OAuth"}

    def discover(self):
        workspaces = self.request("GET", "/v1/workspaces")
        records = []
        for workspace in workspaces:
            for resource in workspace.get("resources", []):
                details = self.request("GET", "/v1/resources/" + segment(resource["id"]))
                records.append({"route": self.route, "device": resource["id"], "workspace": workspace["id"],
                    "qubits": details["available_qubits"], "native_gates": ["u1q", "rz", "rxx", "measure"],
                    "coupling": [], "all_to_all": True, "directed_connectivity": False,
                    "supports": {"reset": False, "mid_circuit_measure": False, "feedforward": False},
                    "parameter_units": "radians", "formats": ["aqt-native"], "capability_verified": True,
                    "raw": details})
        return records

    def submit(self, artifact, options):
        self.validate(artifact, options)
        if options.shots > 2000:
            raise QStackError("AQT cloud supports at most 2000 shots per circuit")
        operations, measured = [], False
        for item in instructions(artifact.physical_ir):
            op, q, p = item["op"].lower(), item.get("qubits", []), item.get("params", [])
            if op == "barrier":
                continue
            if op == "measure":
                measured = True
                continue
            if measured:
                raise QStackError("AQT only supports terminal measurements", "UNSUPPORTED_CAPABILITY")
            if op in {"u1q", "r"} and len(q) == 1 and len(p) == 2:
                theta, phi = p[0] / pi, p[1] / pi
                if not (0 <= theta <= 1 and 0 <= phi <= 2):
                    raise QStackError("AQT R requires compiler-lowered theta in [0,pi], phi in [0,2pi]")
                operations.append({"operation": "R", "qubit": q[0], "theta": theta, "phi": phi})
            elif op == "rz" and len(q) == len(p) == 1:
                operations.append({"operation": "RZ", "qubit": q[0], "phi": p[0] / pi})
            elif op == "rxx" and len(q) == 2 and len(p) == 1:
                if not 0 <= p[0] / pi <= 0.5:
                    raise QStackError("AQT RXX requires compiler-lowered theta in [0,pi/2]")
                operations.append({"operation": "RXX", "qubits": q, "theta": p[0] / pi})
            else:
                raise QStackError(f"AQT cannot serialize physical gate '{op}'", "UNSUPPORTED_GATE")
        if not measured:
            raise QStackError("AQT circuit requires a terminal measurement")
        operations.append({"operation": "MEASURE"})
        body = {"job_type": "quantum_circuit", "label": options.name,
                "payload": {"circuits": [{"repetitions": options.shots, "quantum_circuit": operations,
                                           "number_of_qubits": artifact.physical_ir["num_qubits"]}]}}
        workspace = self.need("workspace")
        resource = self.config.get("device") or self.config.get("resource") or artifact.target
        response = self.request("POST", f"/v1/submit/{segment(workspace)}/{segment(resource)}", data=body)
        return self.receipt(artifact, response["job"]["job_id"], workspace=workspace, resource=resource)

    def status(self, receipt):
        raw = self.request("GET", "/v1/result/" + segment(receipt.job_id))
        state = raw.get("response", {}).get("status", "unknown")
        return {"job_id": receipt.job_id, "status": "completed" if state == "finished" else state, "raw": raw}

    def results(self, receipt):
        raw = self.status(receipt)["raw"]
        response = raw.get("response", {})
        counts = None
        if response.get("status") == "finished":
            samples = response.get("result", {}).get("0")
            if samples is not None:
                mapped = []
                for sample in samples:
                    bits = [0] * receipt.metadata["num_clbits"]
                    for mapping in receipt.metadata["measurement_mapping"]:
                        bits[mapping["clbit"]] = sample[mapping["qubit"]]
                    mapped.append("".join(str(x) for x in reversed(bits)))
                counts = dict(Counter(mapped))
        return self.result(receipt, raw, counts, bit_order="classical-msb-left")

    def cancel(self, receipt):
        self.unsupported("cancel", "AQT connector 0.4 exposes no public cancellation method")
