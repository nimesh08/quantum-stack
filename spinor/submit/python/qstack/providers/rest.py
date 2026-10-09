"""Direct HTTP contracts. Source links and version boundaries are in CONTRACTS.md."""
from __future__ import annotations

from . import submission_objects

import base64
import json
import re
from math import isfinite, pi
from urllib.parse import urlencode

from qstack.models import QStackError
from .base import RestAdapter, counts_dict, plain, segment
from .native import instructions


class IonQAdapter(RestAdapter):
    route = "ionq"
    base_url = "https://api.ionq.co/v0.4"

    def headers(self):
        return {"Authorization": "apiKey " + self.need("api_key", "token")}

    def check_auth(self):
        self.request("GET", "/whoami")
        return {"route": self.route, "authenticated": True}

    def discover(self):
        data = self.request("GET", "/backends")
        rows = data if isinstance(data, list) else data.get("backends", [])
        return [{"route": self.route, "device": r.get("backend", r.get("name")),
                 "qubits": r.get("qubits"), "native_gates": ["rzz" if g == "zz" else g for g in r.get("supported_native_gates", [])] + ["measure"],
                 "coupling": [], "all_to_all": True, "directed_connectivity": False,
                 "supports": {"reset": False, "mid_circuit_measure": False, "feedforward": False},
                 "formats": ["ionq-native-json"], "parameter_units": "radians",
                 "capability_verified": bool(r.get("qubits") and r.get("supported_native_gates")),
                 "capability_sources": ["https://docs.ionq.com/api-reference/v0.4/backends/get-backends", "https://docs.ionq.com/features/getting-started-with-native-gates"],
                 "raw": r} for r in rows]

    def estimate(self, artifact, options):
        self.validate(artifact, options, {"ionq-native-json", "ionq-json", "ionq.circuit.v1", "native-json"})
        circuit = json.loads(artifact.program_text())
        circuit = circuit.get("input", circuit)
        if circuit.get("gateset") != "native":
            raise QStackError("IonQ estimates require the exact native artifact")
        one = two = 0
        for instruction in circuit.get("circuit", []):
            if instruction.get("gate") in {"gpi", "gpi2"}:
                one += 1
            elif instruction.get("gate") in {"ms", "zz"}:
                two += 1
            else:
                raise QStackError("Cannot estimate a non-native IonQ instruction")
        query = {"backend": self.config.get("device") or artifact.target, "type": "ionq.circuit.v1",
                 "qubits": circuit["qubits"], "shots": options.shots,
                 "1q_gates": one, "2q_gates": two, "error_mitigation": "false"}
        raw = self.request("GET", "/jobs/estimate?" + urlencode(query))
        amount = raw.get("estimated_total_cost")
        usd = amount if str(raw.get("estimated_unit", "")).upper() == "USD" else None
        if usd is not None and (not isinstance(usd, (int, float)) or isinstance(usd, bool) or not isfinite(usd) or usd < 0):
            raise QStackError("Provider returned an invalid USD cost estimate", "INVALID_RESPONSE")
        return {"usd": usd, "raw": raw, "source": "IonQ authenticated v0.4 job estimate", "binding": False}

    def submit(self, artifact, options):
        self.validate(artifact, options, {"ionq-native-json", "ionq-json", "ionq.circuit.v1", "native-json"})
        target = self.config.get("device") or artifact.target
        body = submission_objects.ionq_body(artifact, shots=options.shots, name=options.name, target=target)
        response = self.request("POST", "/jobs", data=body)
        return self.receipt(artifact, response.get("id"), backend=target)

    def status(self, receipt):
        return self.request("GET", f"/jobs/{segment(receipt.job_id)}")

    def results(self, receipt):
        job = self.status(receipt)
        artifacts = {}
        for kind, identifier in job.get("results", {}).items():
            identifiers = identifier if isinstance(identifier, list) else [identifier]
            downloaded = []
            for item in identifiers:
                artifact_id = item.get("id") if isinstance(item, dict) else item
                if artifact_id:
                    downloaded.append(self.request("GET", f"/jobs/{segment(receipt.job_id)}/artifacts/{segment(artifact_id)}"))
            artifacts[kind] = downloaded
        # Probabilities/mitigated histograms must never be rounded into shot counts.
        return self.result(receipt, {"job": job, "artifacts": artifacts},
                           result_kind="provider_artifacts", bit_order="provider-native")

    def cancel(self, receipt):
        return self.request("PUT", f"/jobs/{segment(receipt.job_id)}/status/cancel")


class AnyonAdapter(RestAdapter):
    route = "anyon"
    capabilities = {**RestAdapter.capabilities, "cancel": False}

    def headers(self):
        encoded = base64.b64encode((self.need("user", "username") + ":" + self.need("access_token", "api_key", "token")).encode()).decode()
        headers = {"Authorization": "Basic " + encoded}
        if self.config.get("realm"):
            headers["X-Realm"] = self.config["realm"]
        return headers

    def discover(self):
        machine = self.config.get("device", "yukon")
        data = self.request("GET", "/machines?" + urlencode({"machineName": machine}))
        records = []
        for row in data.get("items", []):
            excluded = set(row.get("disconnectedQubits", []))
            excluded_edges = {tuple(sorted(pair)) for pair in row.get("disconnectedConnections", [])}
            count = row.get("qubitCount", 0)
            known = row["name"].lower() == "yukon" and row.get("connectivity") == "linear" and count == 6
            edges = [[i, i + 1] for i in range(count - 1) if i not in excluded and i + 1 not in excluded and (i, i + 1) not in excluded_edges] if known else []
            records.append({"route": self.route, "device": row["name"], "qubits": count,
                "native_gates": ["id", "x", "y", "z", "sx", "sxdg", "rz", "t", "tdg", "s", "sdg", "cz", "measure"] if known else [],
                "coupling": edges, "all_to_all": False, "directed_connectivity": False,
                "unavailable_qubits": sorted(excluded), "parameter_units": "radians",
                "supports": {"reset": False, "mid_circuit_measure": False, "feedforward": False},
                "formats": ["anyon-json"], "capability_verified": known,
                "capability_sources": ["https://github.com/SnowflurrySDK/Snowflurry.jl/blob/master/src/anyon/anyon.jl"], "raw": row})
        return records

    def submit(self, artifact, options):
        self.validate(artifact, options, {"anyon-json", "snowflurry-json", "native-json"})
        machine = self.config.get("device") or artifact.target
        body = submission_objects.anyon_body(artifact, shots=options.shots, name=options.name,
            target=machine, project=self.config.get("project"))
        response = self.request("POST", "/jobs", data=body)
        return self.receipt(artifact, response.get("job", {}).get("id", response.get("id")), machine=machine)

    def status(self, receipt):
        raw = self.request("GET", f"/jobs/{segment(receipt.job_id)}")
        state = raw.get("job", {}).get("status", {})
        return {"job_id": receipt.job_id, "status": state.get("type", "unknown") if isinstance(state, dict) else state,
                "raw": raw}

    def results(self, receipt):
        data = self.status(receipt)["raw"]
        histogram = data.get("result", {}).get("histogram")
        return self.result(receipt, data, counts_dict(histogram), bit_order="provider-native")

    def cancel(self, receipt):
        self.unsupported("cancel", "Snowflurry's public client contract exposes no cancellation endpoint")


class AliceBobAdapter(RestAdapter):
    route = "alicebob"
    base_url = "https://api-gcp.alice-bob.com"
    capabilities = {**RestAdapter.capabilities, "vendor_transpilation": True}

    def headers(self):
        # Felis supplies an already encoded Basic credential; do not encode it twice.
        return {"Authorization": "Basic " + self.need("api_key", "token")}

    def discover(self):
        rows = self.request("GET", "/v1/targets/")
        records = []
        aliases = {"mz": "measure", "m": "measure", "mx": "measure_x"}
        for row in rows:
            gates = []
            for instruction in row.get("instructions", []):
                match = re.search(r"__quantum__qis__(\w+)__body", instruction.get("signature", ""))
                if match and match[1] != "read_result":
                    gates.append(aliases.get(match[1], match[1]))
            # One exposed qubit needs no inferred topology. Larger emulators require an explicit topology contract.
            records.append({"route": self.route, "device": row["name"], "qubits": row.get("numQubits"),
                "native_gates": sorted(set(gates)), "coupling": [], "all_to_all": False,
                "directed_connectivity": False, "parameter_units": "radians",
                "supports": {"reset": "reset" in gates, "mid_circuit_measure": False, "feedforward": False},
                "formats": ["qir-text"], "capability_verified": bool(gates) and row.get("numQubits") == 1,
                "capability_sources": ["https://felis.alice-bob.com/docs/reference/supported_instructions/"], "raw": row})
        return records

    def submit(self, artifact, options):
        self.validate(artifact, options, {"qir", "qir-text", "llvm-ir", "qir.ll"})
        if artifact.target_snapshot.get("qir_platform", "standard") != "standard":
            raise QStackError("Felis requires its standard QIR instruction contract; recompile for Alice & Bob", "UNSUPPORTED_FORMAT")
        # Only the published static Felis subset is currently verified. Reject
        # control flow before the first create-job request, including direct API use.
        list(instructions(artifact.physical_ir))
        advertised = {item["signature"].split(":", 1)[0]
                      for item in artifact.target_snapshot.get("raw", {}).get("instructions", []) if "signature" in item}
        used = set(re.findall(r"\bcall\s+\w+\s+@(__quantum__qis__\w+)\(", artifact.program_text()))
        if advertised and used - advertised:
            raise QStackError("QIR calls instructions absent from the Felis target contract", "UNSUPPORTED_GATE")
        params = dict(self.config.get("input_params", {}))
        params["nbShots"] = options.shots
        response = self.request("POST", "/v1/jobs/", data={"inputDataFormat": "HUMAN_QIR",
            "outputDataFormat": "HISTOGRAM", "target": self.config.get("device") or artifact.target,
            "inputParams": params})
        receipt = self.receipt(artifact, response.get("id"))
        try:
            self.request("POST", f"/v1/jobs/{segment(receipt.job_id)}/input", files={"input": submission_objects.upload_payload(artifact)})
        except QStackError as exc:
            raise QStackError(f"Alice & Bob job {receipt.job_id} was created but input upload failed; retain this ID and do not resubmit automatically", "PARTIAL_SUBMISSION") from exc
        return receipt

    def status(self, receipt):
        return self.request("GET", f"/v1/jobs/{segment(receipt.job_id)}")

    def results(self, receipt):
        output = self.request("GET", f"/v1/jobs/{segment(receipt.job_id)}/output", text=True)
        # Felis HISTOGRAM is CSV; preserve the original output rather than guessing its bit ordering.
        return self.result(receipt, {"output": output}, result_kind="felis-histogram", bit_order="provider-native")

    def cancel(self, receipt):
        self.request("DELETE", f"/v1/jobs/{segment(receipt.job_id)}")
        return {"job_id": receipt.job_id, "cancellation_requested": True}
