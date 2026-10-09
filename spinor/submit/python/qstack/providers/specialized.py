"""Provider-specific optional SDK contracts without vendor circuit transpilers."""
from __future__ import annotations

import base64
import json
from uuid import UUID
from qstack.models import QStackError
from .base import Adapter, counts_dict, optional, plain
from .capability import account_capability, calibration_digest
from .calibration import rigetti_calibrations


class RigettiAdapter(Adapter):
    route = "rigetti"
    capabilities = {**Adapter.capabilities, "status": False, "login": True,
                    "mandatory_vendor_translation": True}

    def client(self):
        if not hasattr(self, "_client"):
            self._client = self.config.get("_client")
            if self._client is None:
                self._client = optional("qcs_sdk.client", "rigetti").QCSClient.load(self.config.get("sdk_profile"))
        return self._client

    def login(self):
        self._client = optional("qcs_sdk.client", "rigetti").QCSClient.load_with_login(self.config.get("sdk_profile"))
        return {"route": self.route, "authenticated": True, "method": "QCS browser OAuth"}

    def discover(self):
        processors = optional("qcs_sdk.qpu", "rigetti").list_quantum_processors(client=self.client())
        records = []
        for processor in processors:
            isa = optional("qcs_sdk.qpu.isa", "rigetti").get_instruction_set_architecture(str(processor), client=self.client())
            gates, edges, loci, nodes = [], set(), {}, set()
            for instruction in isa.instructions:
                name = {"I": "id", "MEASURE": "measure"}.get(instruction.name, instruction.name.lower())
                if instruction.sites:
                    gates.append(name)
                loci[name] = [list(site.node_ids) for site in instruction.sites]
                for site in instruction.sites:
                    nodes.update(site.node_ids)
                    if len(site.node_ids) == 2:
                        edges.add(tuple(site.node_ids))
            records.append({"route": self.route, "device": str(processor), "qubits": max(nodes, default=-1) + 1,
                "available_qubits": sorted(nodes), "native_gates": sorted(set(gates)), "gate_loci": loci,
                "coupling": sorted(map(list, edges)), "all_to_all": False, "directed_connectivity": False,
                "supports": {"reset": "reset" in gates, "mid_circuit_measure": False, "feedforward": False},
                "parameter_units": "radians", "formats": ["quil"], "capability_verified": bool(gates and nodes),
                **rigetti_calibrations(isa),
                "raw": json.loads(isa.json()), "capability_sources": ["https://rigetti.github.io/qcs-sdk-rust/qcs_sdk/qpu/isa.html"]})
        return records

    def submit(self, artifact, options):
        self.validate(artifact, options, {"quil", "native-quil"})
        target = self.config.get("device") or artifact.target
        # Hardware control translation is mandatory; quilc gate decomposition is never called.
        translated = optional("qcs_sdk.qpu.translation", "rigetti").translate(
            native_quil=artifact.program_text(), num_shots=options.shots,
            quantum_processor_id=target, client=self.client())
        job_id = optional("qcs_sdk.qpu.api", "rigetti").submit(
            program=translated.program, patch_values={}, quantum_processor_id=target, client=self.client())
        return self.receipt(artifact, job_id, device=target, ro_sources=plain(translated.ro_sources))

    def status(self, receipt):
        self.unsupported("status", "QCS execution API has no separate nonblocking state query; retrieve results or use the QCS dashboard")

    def results(self, receipt):
        result = optional("qcs_sdk.qpu.api", "rigetti").retrieve_results(
            receipt.job_id, quantum_processor_id=receipt.metadata.get("device", receipt.target), client=self.client())
        raw = {"buffers": {key: {"data": plain(value.data), "shape": plain(value.shape), "dtype": str(value.dtype)}
                           for key, value in result.buffers.items()},
               "memory": {key: plain(value.inner()) for key, value in result.memory.items()},
               "execution_duration_microseconds": result.execution_duration_microseconds,
               "ro_sources": receipt.metadata.get("ro_sources")}
        return self.result(receipt, raw, result_kind="readout-buffers")

    def cancel(self, receipt):
        optional("qcs_sdk.qpu.api", "rigetti").cancel_job(
            receipt.job_id, quantum_processor_id=receipt.metadata.get("device", receipt.target), client=self.client())
        return {"job_id": receipt.job_id, "cancellation_requested": True, "guaranteed": False}


class IQMAdapter(Adapter):
    route = "iqm"
    capabilities = {**Adapter.capabilities, "mandatory_vendor_translation": True}

    def client(self):
        if not hasattr(self, "_client"):
            self._client = self.config.get("_client")
            if self._client is None:
                kwargs = {"iqm_server_url": self.need("url"), "client_signature": "qstack"}
                if self.config.get("quantum_computer") or self.config.get("device"):
                    kwargs["quantum_computer"] = self.need("quantum_computer", "device")
                if self.config.get("api_key") or self.config.get("token"):
                    kwargs["token"] = self.need("api_key", "token")
                elif self.config.get("tokens_file") or self.config.get("credentials_file"):
                    kwargs["tokens_file"] = self.need("tokens_file", "credentials_file")
                self._client = optional("iqm.iqm_client", "iqm").IQMClient(**kwargs)
        return self._client

    def discover(self):
        client = self.client()
        dqa = client.get_dynamic_quantum_architecture()
        raw = plain(dqa)
        computational = list(dqa.qubits)
        resonators = list(dqa.computational_resonators)
        labels = computational + resonators
        if len(set(labels)) != len(labels):
            raise QStackError("IQM architecture repeats a component label", "INVALID_RESPONSE")
        gates, coupling, gate_loci = [], [], {}
        for name, info in dqa.gates.items():
            if name not in {"prx", "cz", "move", "measure", "reset"}:
                continue
            loci = [locus for locus in info.loci if all(q in labels for q in locus)]
            if name == "move" and any(len(locus) != 2 or locus[0] not in computational or locus[1] not in resonators for locus in loci):
                raise QStackError("IQM MOVE loci must be ordered (qubit, resonator)", "INVALID_RESPONSE")
            if not loci:
                continue
            native_name = {"prx": "u1q"}.get(name, name)
            gates.append(native_name)
            gate_loci[native_name] = [[labels.index(q) for q in locus] for locus in loci]
            for locus in loci:
                if len(locus) == 2:
                    coupling.append([labels.index(q) for q in locus])
        return [{"route": self.route, "vendor": "iqm", "device": client.quantum_computer_name, "qubits": len(labels),
                 "qubit_labels": labels, "native_gates": gates, "gate_loci": gate_loci,
                 "computational_qubits": list(range(len(computational))),
                 "resonator_qubits": list(range(len(computational), len(labels))),
                 "computational_resonators": resonators, "coupling": coupling, "all_to_all": False,
                 "directed_connectivity": False, "formats": ["iqm-json"], "parameter_units": "radians",
                 "supports": {"reset": "reset" in gates, "mid_circuit_measure": False, "feedforward": False},
                 "capability_verified": bool(computational and "u1q" in gates and "measure" in gates),
                 "calibration_set_id": str(dqa.calibration_set_id), "raw": raw,
                 "capability_sources": ["https://docs.iqm.tech/iqm-client/integration_guide.html",
                                        "https://docs.iqm.tech/iqm-client/iqm.iqm_client.transpile.html"]}]

    def submit(self, artifact, options):
        self.validate(artifact, options, {"iqm-json", "iqm-circuit-json", "native-json"})
        data = json.loads(artifact.program_text())
        circuit_data = data.get("circuits", [data])[0]
        op_type = optional("iqm.pulse.builder", "iqm").CircuitOperation
        circuit_type = optional("iqm.pulse.circuit_operations", "iqm").Circuit
        ops = [op_type(name=i["name"], locus=tuple(i["locus"]), args=i.get("args", {}))
               for i in circuit_data["instructions"]]
        circuit = circuit_type(name=options.name, instructions=tuple(ops))
        kwargs = {"shots": options.shots}
        calibration = artifact.target_snapshot.get("calibration_set_id") or self.config.get("calibration_set_id")
        if calibration:
            kwargs["calibration_set_id"] = UUID(calibration)
        job = self.client().submit_circuits([circuit], **kwargs)
        physical_measurements = [i for i in artifact.physical_ir.get("instructions", []) if i.get("op") == "measure"]
        keys = [i["args"]["key"] for i in circuit_data["instructions"] if i["name"] == "measure"]
        key_mapping = [{"key": key, "clbit": inst["clbits"][0]} for key, inst in zip(keys, physical_measurements)]
        return self.receipt(artifact, job.job_id, calibration_set_id=calibration, measurement_keys=key_mapping)

    def status(self, receipt):
        job = self.client().get_job(UUID(receipt.job_id))
        return {"job_id": receipt.job_id, "status": plain(job.update())}

    def results(self, receipt):
        job = self.client().get_job(UUID(receipt.job_id))
        state = plain(job.update())
        if state != "completed":
            raise QStackError(f"IQM job has no completed result (status {state})", "RESULT_NOT_READY")
        return self.result(receipt, job.result(), result_kind="measurement-registers",
                           measurement_keys=receipt.metadata.get("measurement_keys", []))

    def cancel(self, receipt):
        self.client().get_job(UUID(receipt.job_id)).cancel()
        return {"job_id": receipt.job_id, "cancellation_requested": True}


class OQCAdapter(Adapter):
    route = "oqc"
    capabilities = {**Adapter.capabilities, "mandatory_vendor_translation": True}

    def client(self):
        if not hasattr(self, "_client"):
            self._client = self.config.get("_client")
            if self._client is None:
                self._client = optional("qcaas_client.client", "oqc").OQCClient(
                    url=self.need("url"), authentication_token=self.need("access_token", "api_key", "token"))
        return self._client

    def discover(self):
        client = self.client()
        target = self.config.get("device") or self.config.get("qpu_id")
        if self.config.get("capability_snapshot") and not target:
            raise QStackError("An OQC capability_snapshot requires an explicit device QPU ID", "INVALID_CONFIG")
        qpus = client.get_qpus()
        if qpus is None:
            raise QStackError("OQC client did not initialize or return its active QPUs", "AUTHENTICATION_FAILED")
        if target and target not in {row["id"] for row in qpus}:
            raise QStackError("Configured OQC QPU ID is absent from this account's active QPUs", "TARGET_UNAVAILABLE")
        records = []
        for qpu in qpus:
            if target and qpu["id"] != target:
                continue
            calibration_unavailable = False
            try:
                calibration = plain(client.get_calibration(qpu_id=qpu["id"]))
            except Exception as error:
                # The documented Fermioniq emulator route has no calibration
                # endpoint. One emulator must not hide the account's real QPUs.
                if getattr(error, "server_error_code", None) != 405:
                    raise
                calibration, calibration_unavailable = None, True
            record = {"route": self.route, "vendor": "oqc", "device": qpu["id"], "name": qpu.get("name"),
                "formats": ["openqasm2", "openqasm3", "qir-text", "qir-bitcode"],
                "capability_verified": False, "calibration_sha256": None if calibration_unavailable else calibration_digest(calibration),
                "readiness_reason": "This OQC endpoint does not expose calibration data" if calibration_unavailable else
                    "OQC publishes benchmarking data without a native gate/locus schema; supply the calibration-bound account capability contract",
                "capability_sources": ["https://docs.oqc.app/system_information.html", "https://docs.oqc.app/task_management.html"],
                "raw": {"qpu": {key: qpu[key] for key in ("id", "name", "active") if key in qpu}, "calibration": calibration}}
            if self.config.get("capability_snapshot") and target:
                if calibration_unavailable:
                    raise QStackError("Cannot verify a calibration-bound contract for an endpoint without calibration data", "TARGET_INCOMPATIBLE")
                record.update(account_capability(self.config["capability_snapshot"], route=self.route,
                                                 device=qpu["id"], calibration=calibration))
                record["readiness_reason"] = None
            records.append(record)
        return records

    def submit(self, artifact, options):
        self.validate(artifact, options, {"openqasm2", "qasm2", "openqasm3", "qasm3", "qir", "qir-text", "qir-bitcode", "qir.bc"})
        sdk = optional("qcaas_client.client", "oqc")
        compiler = optional("compiler_config.config", "oqc")
        optim = compiler.Tket()
        optim.disable()
        config = compiler.CompilerConfig(repeats=options.shots, optimizations=optim,
                                         results_format=compiler.QuantumResultsFormat().binary_count())
        program = artifact.program_text() if artifact.format.lower() in {"openqasm2", "qasm2", "openqasm3", "qasm3"} else base64.b64encode(artifact.program_bytes()).decode("ascii")
        target = self.config.get("device") or artifact.target
        task = sdk.QPUTask(program=program, qpu_id=target, config=config)
        scheduled = self.client().schedule_tasks(task, qpu_id=target)
        return self.receipt(artifact, scheduled[0].task_id, qpu_id=target)

    def status(self, receipt):
        state = plain(self.client().get_task_status(receipt.job_id, qpu_id=receipt.metadata.get("qpu_id", receipt.target)))
        return {"job_id": receipt.job_id, "status": "FAILED" if state == "EXPIRED" else state, "provider_status": state}

    def results(self, receipt):
        response = self.client().get_task_results(receipt.job_id, qpu_id=receipt.metadata.get("qpu_id", receipt.target))
        if response is None:
            raise QStackError("OQC task has no available result", "RESULT_NOT_READY")
        error = getattr(response, "error_details", None)
        result = response.result
        raw = {"result": result, "metrics": plain(getattr(response, "metrics", {})),
               "error": None if error is None else {"code": error.error_code, "message": error.error_message}}
        return self.result(receipt, raw, counts_dict(result.get("c")) if isinstance(result, dict) else None,
                           bit_order="provider-register-order")

    def cancel(self, receipt):
        self.client().cancel_task(receipt.job_id, qpu_id=receipt.metadata.get("qpu_id", receipt.target))
        return {"job_id": receipt.job_id, "cancellation_requested": True}


class QuantinuumAdapter(Adapter):
    route = "quantinuum"
    capabilities = {**Adapter.capabilities, "login": True, "mandatory_vendor_translation": True}

    def client(self):
        return self.config.get("_client") or optional("qnexus", "quantinuum")

    def login(self):
        qnx = self.client()
        if (self.config.get("user") or self.config.get("username")) and self.config.get("password"):
            qnx.auth.login_no_interaction(self.need("user", "username"), self.config["password"], region=self.config.get("region"))
        else:
            qnx.auth.login(region=self.config.get("region"))
        return {"route": self.route, "authenticated": True, "method": "Nexus SDK session"}

    def discover(self):
        qnx = self.client()
        # The current issuer mapper only includes QuantinuumConfig and omits
        # HeliosConfig. Fetch metadata then select the documented native systems.
        devices = qnx.devices.get_all()
        records = []
        for device in devices:
            if not (str(device.device_name).lower().startswith(("h2-", "helios-")) or
                    any(name in str(device.backend_name).lower() for name in ("quantinuum", "helios"))):
                continue
            info = device.stored_backend_info
            architecture = info.device
            nodes = [node.unitid for node in architecture.nodes]
            count = architecture.n_nodes if architecture.n_nodes is not None else len(nodes)
            def index(unitid):
                return nodes.index(unitid)
            edges = [[index(edge.unitid_from), index(edge.unitid_to)] for edge in architecture.edges]
            # Only these documented QIR QIS operations are native; backend gate_set can also include high-level gates.
            known = str(device.device_name).lower().startswith(("h2-", "helios-"))
            gates = ["u1q", "rz", "rzz", "measure", "reset"] if known else []
            records.append({"route": self.route, "device": device.device_name, "qubits": count,
                "qir_platform": "quantinuum-helios" if str(device.device_name).lower().startswith("helios") else "quantinuum-h2",
                "native_gates": gates, "coupling": edges, "all_to_all": architecture.fully_connected,
                "directed_connectivity": False, "parameter_units": "radians",
                "supports": {"reset": info.supports_reset, "mid_circuit_measure": info.supports_midcircuit_measurement,
                             "feedforward": info.supports_fast_feedforward},
                "formats": ["qir-bitcode", "hugr"] if str(device.device_name).lower().startswith("helios") else ["qir-bitcode"],
                "capability_verified": bool(known and count and (architecture.fully_connected or edges)), "raw": plain(device),
                "capability_sources": ["https://docs.quantinuum.com/nexus/trainings/notebooks/basics/qir/index.html"]})
        return records

    def submit(self, artifact, options):
        self.validate(artifact, options, {"qir-bitcode", "qir.bc", "hugr"})
        qnx = self.client()
        target = self.config.get("device") or artifact.target
        project_name = self.need("project")
        try:
            project_id = UUID(project_name)
        except ValueError:
            project = qnx.projects.get(name=project_name)
        else:
            project = qnx.projects.get(id=project_id)
        kwargs = {"name": options.name, "project": project}
        if artifact.format == "hugr":
            if not target.lower().startswith("helios"):
                raise QStackError("HUGR submission requires a Helios target", "UNSUPPORTED_FORMAT")
            ref = qnx.hugr.upload(artifact.program_bytes(), **kwargs)
        else:
            ref = qnx.qir.upload(qir=artifact.program_bytes(), **kwargs)
        backend = (qnx.models.HeliosConfig(system_name=target) if target.lower().startswith("helios")
                   else qnx.models.QuantinuumConfig(device_name=target, no_opt=True,
                       allow_implicit_swaps=False, allow_2q_gate_rebase=False, simplify_initial=False))
        execute_args = {"programs": [ref], "n_shots": [options.shots], "backend_config": backend, **kwargs}
        # Nexus max_cost is HQC, not USD. Do not silently interpret a dollar cap as HQC.
        if self.config.get("max_cost_hqc") is not None:
            execute_args["max_cost"] = float(self.config["max_cost_hqc"])
        job = qnx.start_execute_job(**execute_args)
        return self.receipt(artifact, job.id, project=self.config["project"], device=target)

    def status(self, receipt):
        qnx = self.client()
        raw = qnx.jobs.status(qnx.jobs.get(id=receipt.job_id))
        state = plain(getattr(raw, "status", raw))
        # Nexus terminal credit/termination states must not be polled forever.
        state = "failed" if state in {"DEPLETED", "TERMINATED"} else state
        return {"job_id": receipt.job_id, "status": state, "raw": plain(raw)}

    def results(self, receipt):
        qnx = self.client()
        refs = qnx.jobs.results(qnx.jobs.get(id=receipt.job_id))
        downloaded = [plain(ref.download_result()) for ref in refs]
        return self.result(receipt, downloaded, result_kind="qir-or-register-results")

    def cancel(self, receipt):
        qnx = self.client()
        qnx.jobs.cancel(qnx.jobs.get(id=receipt.job_id))
        return {"job_id": receipt.job_id, "cancellation_requested": True}
