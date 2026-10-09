"""Cloud SDK adapters; SDKs are used only for transport and serialization."""
from __future__ import annotations

import subprocess
from decimal import Decimal, InvalidOperation
from urllib.parse import urlsplit
from qstack.models import QStackError
from .base import Adapter, counts_dict, optional, plain
from .native import cirq_circuit, cirq_measurement_keys, qiskit_circuit, qiskit_target_record, validate_qiskit_target


class IBMAdapter(Adapter):
    route = "ibm"

    def client(self):
        if not hasattr(self, "_service"):
            self._service = self.config.get("_client")
            if self._service is None:
                runtime = optional("qiskit_ibm_runtime", "ibm")
                kwargs = {"channel": "ibm_quantum_platform"}
                if self.config.get("api_key"):
                    kwargs["token"] = self.config["api_key"]
                if self.config.get("instance_crn"):
                    kwargs["instance"] = self.config["instance_crn"]
                if self.config.get("sdk_profile"):
                    kwargs["name"] = self.config["sdk_profile"]
                if self.config.get("credentials_file"):
                    kwargs["filename"] = self.config["credentials_file"]
                self._service = runtime.QiskitRuntimeService(**kwargs)
        return self._service

    def discover(self):
        return [qiskit_target_record(self.route, b, ["qiskit-native"]) for b in self.client().backends()]

    def submit(self, artifact, options):
        self.validate(artifact, options)
        circuit = qiskit_circuit(artifact)
        backend = self.client().backend(self.config.get("device") or artifact.target)
        validate_qiskit_target(artifact.physical_ir, getattr(backend, "target", None))
        # The ISA circuit is already mapped. SamplerV2 performs no local transpilation.
        sampler = optional("qiskit_ibm_runtime", "ibm").SamplerV2(mode=backend)
        job = sampler.run([circuit], shots=options.shots)
        return self.receipt(artifact, job.job_id(), device=backend.name)

    def status(self, receipt):
        return {"job_id": receipt.job_id, "status": plain(self.client().job(receipt.job_id).status())}

    def results(self, receipt):
        result = self.client().job(receipt.job_id).result()
        pubs = []
        for pub in result:
            registers = {name: plain(value.get_counts()) for name, value in pub.data.items() if hasattr(value, "get_counts")}
            pubs.append({"registers": registers, "metadata": plain(pub.metadata)})
        counts = counts_dict(pubs[0]["registers"].get("c")) if len(pubs) == 1 else None
        return self.result(receipt, pubs, counts, bit_order="classical-msb-left")

    def cancel(self, receipt):
        result = self.client().job(receipt.job_id).cancel()
        return {"job_id": receipt.job_id, "cancellation_requested": True, "provider_response": plain(result)}


class AWSAdapter(Adapter):
    route = "aws"
    capabilities = {**Adapter.capabilities, "login": True}

    def session(self):
        if not hasattr(self, "_session"):
            if self.config.get("_client") is not None:
                self._session = self.config["_client"]
            else:
                kwargs = {}
                for key, arg in (("sdk_profile", "profile_name"), ("region", "region_name"),
                                 ("access_key_id", "aws_access_key_id"), ("secret_access_key", "aws_secret_access_key"),
                                 ("session_token", "aws_session_token")):
                    if self.config.get(key):
                        kwargs[arg] = self.config[key]
                boto = optional("boto3", "aws").Session(**kwargs)
                self._session = optional("braket.aws", "aws").AwsSession(boto_session=boto)
        return self._session

    def check_auth(self):
        self.session().boto_session.client("sts").get_caller_identity()
        return {"route": self.route, "authenticated": True}

    def login(self):
        command = ["aws", "sso", "login"]
        if self.config.get("sdk_profile"):
            command += ["--profile", self.config["sdk_profile"]]
        try:
            subprocess.run(command, check=True)
        except (OSError, subprocess.CalledProcessError):
            raise QStackError("AWS SSO login failed; install/configure AWS CLI and an SSO profile", "AUTHENTICATION_FAILED") from None
        return {"route": self.route, "authenticated": True, "method": "AWS SSO"}

    def discover(self):
        sdk = optional("braket.aws", "aws")
        records = []
        for device in sdk.AwsDevice.get_devices(aws_session=self.session()):
            raw = plain(device.properties)
            paradigm = raw.get("paradigm", {})
            native = paradigm.get("nativeGateSet", [])
            connection = paradigm.get("connectivity", {})
            coupling = [[int(a), int(b)] for a, bs in connection.get("connectivityGraph", {}).items() for b in bs]
            provider_name = str(getattr(device, "provider_name", "")).strip().lower()
            vendor = provider_name if provider_name in {"ionq", "rigetti", "iqm", "oqc", "aqt"} else ""
            # Braket device ARNs identify the hardware vendor independently of
            # the AWS transport route. Never guess it from a device nickname.
            resource = device.arn.split(":", 5)[-1].split("/")
            if not vendor and len(resource) == 4 and resource[:2] == ["device", "qpu"] and resource[2] in {"ionq", "rigetti", "iqm", "oqc", "aqt"}:
                vendor = resource[2]
            records.append({"route": self.route, "device": device.arn, "vendor": vendor, "qubits": paradigm.get("qubitCount"),
                "native_gates": [{"zz": "rzz", "xx": "rxx", "prx": "u1q"}.get(g.lower(), g.lower()) for g in native], "coupling": coupling,
                "all_to_all": connection.get("fullyConnected", False), "directed_connectivity": False,
                "supports": {"reset": False, "mid_circuit_measure": False, "feedforward": False},
                "formats": ["openqasm3"], "parameter_units": "radians",
                "capability_verified": bool(native and connection), "raw": raw})
        return records

    def estimate(self, artifact, options):
        self.validate(artifact, options)
        arn = self.config.get("device") or artifact.target
        parts = arn.split(":", 5)
        if len(parts) != 6 or "/qpu/" not in parts[5] or not parts[3]:
            return {"usd": None, "reason": "Only regional on-demand QPU task/shot prices are estimable before execution"}
        name = parts[5].rsplit("/", 1)[-1]
        name = name[:1].upper() + name[1:]
        if "Ibex" in name:
            name = name.replace("Ibex", "IBEX")
        search = optional("braket.tracking.pricing", "aws").price_search
        records = []
        for family in ("Quantum Task", "Quantum Task-Shot"):
            rows = search(**{"Region Code": parts[3], "DeviceName": name, "Product Family": family})
            if len(rows) != 1 or rows[0].get("Currency") != "USD":
                return {"usd": None, "reason": "AWS Price List did not identify one current USD task and shot rate"}
            records.append(rows[0])
        try:
            task, shot = (Decimal(row["PricePerUnit"]) for row in records)
        except (KeyError, InvalidOperation):
            raise QStackError("AWS Price List returned an invalid price", "INVALID_RESPONSE") from None
        if not all(price.is_finite() and price >= 0 for price in (task, shot)):
            raise QStackError("AWS Price List returned an invalid price", "INVALID_RESPONSE")
        amount = task + shot * options.shots
        return {"usd": float(amount), "raw": records, "source": "AWS current Price List via Braket SDK",
                "binding": False, "scope": "one on-demand QPU task plus shots; excludes ancillary services and taxes"}

    def submit(self, artifact, options):
        self.validate(artifact, options, {"openqasm3", "qasm3", "braket-openqasm3"})
        source = artifact.program_text()
        if "#pragma braket verbatim" not in source:
            raise QStackError("AWS owned-compiler route requires a Braket verbatim block", "UNSUPPORTED_FORMAT")
        target = self.config.get("device") or artifact.target
        sdk = optional("braket.aws", "aws")
        device = sdk.AwsDevice(target, aws_session=self.session())
        program = optional("braket.ir.openqasm", "aws").Program(source=source)
        kwargs = {"shots": options.shots, "disable_qubit_rewiring": True}
        if self.config.get("s3_uri"):
            destination = urlsplit(self.config["s3_uri"])
            if destination.scheme != "s3" or not destination.netloc or destination.query or destination.fragment:
                raise QStackError("s3_uri must be s3://bucket/optional-prefix", "INVALID_CONFIG")
            kwargs["s3_destination_folder"] = (destination.netloc, destination.path.lstrip("/"))
        elif self.config.get("s3_bucket"):
            kwargs["s3_destination_folder"] = (self.config["s3_bucket"], self.config.get("s3_prefix", "qstack"))
        task = device.run(program, **kwargs)
        return self.receipt(artifact, task.id, device_arn=target)

    def task(self, receipt):
        return optional("braket.aws", "aws").AwsQuantumTask(receipt.job_id, aws_session=self.session())

    def status(self, receipt):
        return {"job_id": receipt.job_id, "status": self.task(receipt).state()}

    def results(self, receipt):
        task = self.task(receipt)
        state = task.state()
        if state != "COMPLETED":
            raise QStackError(f"AWS task has no completed result (status {state})", "RESULT_NOT_READY")
        result = task.result()
        raw = {"measurements": plain(result.measurements), "measured_qubits": plain(result.measured_qubits),
               "measurement_counts": plain(result.measurement_counts), "task_metadata": plain(result.task_metadata),
               "additional_metadata": plain(result.additional_metadata)}
        return self.result(receipt, raw, counts_dict(dict(result.measurement_counts)), bit_order="measured-qubits-order")

    def cancel(self, receipt):
        result = self.task(receipt).cancel()
        return {"job_id": receipt.job_id, "cancellation_requested": True, "provider_response": plain(result)}


class GoogleAdapter(Adapter):
    route = "google"
    capabilities = {**Adapter.capabilities, "login": True}

    def client(self):
        if not hasattr(self, "_engine"):
            self._engine = self.config.get("_client")
            if self._engine is None:
                kwargs = {"project_id": self.need("project", "project_id")}
                if self.config.get("credentials_file"):
                    creds, _ = optional("google.auth", "google").load_credentials_from_file(
                        self.config["credentials_file"], scopes=["https://www.googleapis.com/auth/cloud-platform"])
                    kwargs["service_args"] = {"credentials": creds}
                self._engine = optional("cirq_google", "google").Engine(**kwargs)
        return self._engine

    def login(self):
        try:
            subprocess.run(["gcloud", "auth", "application-default", "login"], check=True)
        except (OSError, subprocess.CalledProcessError):
            raise QStackError("Google ADC login failed; install Google Cloud CLI or configure credentials_file", "AUTHENTICATION_FAILED") from None
        return {"route": self.route, "authenticated": True, "method": "Google ADC"}

    def discover(self):
        records = []
        for processor in self.client().list_processors():
            spec = processor.get_device_specification()
            labels = list(spec.valid_qubits)
            gate_types = [gate.WhichOneof("gate") for gate in spec.valid_gates]
            aliases = {"syc": "syc", "sqrt_iswap": "sqrt_iswap", "sqrt_iswap_inv": "sqrt_iswap_inv",
                       "cz": "cz", "phased_xz": "phased_xz", "virtual_zpow": "rz", "physical_zpow": "rz",
                       "meas": "measure", "reset": "reset"}
            gates = [aliases[name] for name in gate_types if name in aliases]
            # PhasedXZ includes arbitrary equatorial and Z rotations; the serializer preserves those exactly.
            if "phased_xz" in gates:
                gates.extend(["u1q", "rz"])
            edges = []
            for targets in spec.valid_targets:
                if targets.target_ordering == 1:  # SYMMETRIC in the official proto.
                    for target in targets.targets:
                        if len(target.ids) == 2:
                            edges.append([labels.index(label) for label in target.ids])
            records.append({"route": self.route, "device": processor.processor_id,
                "qubits": len(labels), "qubit_labels": labels, "native_gates": sorted(set(gates)),
                "coupling": edges, "all_to_all": False, "directed_connectivity": False,
                "supports": {"reset": "reset" in gates, "mid_circuit_measure": False, "feedforward": False},
                "parameter_units": "radians", "formats": ["cirq-native"],
                "capability_verified": bool(labels and gates and edges),
                "capability_sources": ["https://github.com/quantumlib/Cirq/blob/main/cirq-google/cirq_google/api/v2/device.proto"],
                "raw": {"device_specification": str(spec), "gate_types": gate_types}})
        return records

    def submit(self, artifact, options):
        self.validate(artifact, options)
        config_name = self.need("device_config_name")
        if self.config.get("run_name") and self.config.get("snapshot_id"):
            raise QStackError("Google run_name and snapshot_id are mutually exclusive")
        circuit = cirq_circuit(artifact, self.config)
        target = self.config.get("device") or artifact.target
        processor = self.client().get_processor(target)
        # Device validation is not decomposition or routing.
        processor.get_device().validate_circuit(circuit)
        kwargs = {"device_config_name": config_name, "repetitions": options.shots,
                  "program_description": options.name}
        for key in ("run_name", "snapshot_id"):
            if self.config.get(key):
                kwargs[key] = self.config[key]
        job = processor.run_sweep(circuit, **kwargs)
        return self.receipt(artifact, job.job_id, program_id=job.program_id,
                            project=self.config.get("project"), device=target,
                            measurement_keys=cirq_measurement_keys(artifact.physical_ir), shots=options.shots,
                            serialization_precision="Engine protobuf float32 gate arguments")

    def job(self, receipt):
        program_id = receipt.metadata.get("program_id")
        if not program_id:
            raise QStackError("Google receipt lacks program_id")
        return self.client().get_program(program_id).get_job(receipt.job_id)

    def status(self, receipt):
        job = self.job(receipt)
        return {"job_id": receipt.job_id, "status": plain(job.execution_status()), "failure": plain(job.failure())}

    def results(self, receipt):
        results = self.job(receipt).results()
        raw = []
        for result in results:
            # Cirq's 2D convenience view rejects repeated keys from older jobs.
            # The 3D records preserve every occurrence and shot correlation.
            records = plain(result.records)
            # Read records directly: ResultDict.measurements can retain a
            # partially initialized cache after raising for repeated keys.
            measurements = ({key: [shot[0] for shot in rows] for key, rows in records.items()}
                if isinstance(records, dict) and all(isinstance(rows, list) and
                    all(isinstance(shot, list) and len(shot) == 1 for shot in rows)
                    for rows in records.values()) else None)
            raw.append({"measurements": measurements, "records": records})
        counts = _google_counts(raw[0]["records"], receipt.metadata) if len(raw) == 1 else None
        return self.result(receipt, raw, counts, result_kind="per-shot-registers",
                           bit_order="classical-msb-first", raw_bit_order="measurement-key-and-occurrence",
                           measurement_keys=receipt.metadata.get("measurement_keys"))

    def cancel(self, receipt):
        self.job(receipt).cancel()
        return {"job_id": receipt.job_id, "cancellation_requested": True}


def _google_counts(records, metadata):
    """Normalize actual correlated samples, including receipts using repeated keys.

    No marginal probabilities are combined. An incomplete or non-binary provider
    payload remains available in raw results without a manufactured histogram.
    """
    width = metadata.get("num_clbits")
    if type(width) is not int or width < 0 or not isinstance(records, dict):
        return None
    mapping = metadata.get("measurement_keys")
    if mapping is None:
        # Before occurrence-specific keys, each cN record held an occurrence
        # axis. Source-ordered receipt mappings identify the last write exactly.
        occurrences, mapping = {}, []
        for entry in metadata.get("measurement_mapping", []):
            bit = entry.get("clbit")
            occurrence = occurrences.get(bit, 0)
            mapping.append({"key": f"c{bit}", "clbit": bit, "occurrence": occurrence})
            occurrences[bit] = occurrence + 1
    if not isinstance(mapping, list) or not mapping:
        return None
    columns, shots = {}, None
    for entry in mapping:
        bit, occurrence = entry.get("clbit"), entry.get("occurrence", 0)
        if type(bit) is not int or not 0 <= bit < width or type(occurrence) is not int or occurrence < 0:
            return None
        values = records.get(entry.get("key"))
        if not isinstance(values, list) or (shots is not None and len(values) != shots):
            return None
        shots = len(values)
        column = []
        for shot in values:
            if not isinstance(shot, list) or occurrence >= len(shot):
                return None
            value = shot[occurrence]
            if not isinstance(value, list) or len(value) != 1 or type(value[0]) not in {int, bool} or value[0] not in {0, 1}:
                return None
            column.append(int(value[0]))
        columns[bit] = column  # source order: the last classical write wins
    if metadata.get("shots", shots) != shots:
        return None
    counts = {}
    for shot in range(shots):
        bitstring = "".join(str(columns[bit][shot] if bit in columns else 0) for bit in reversed(range(width)))
        counts[bitstring] = counts.get(bitstring, 0) + 1
    return counts


class AzureAdapter(Adapter):
    route = "azure"
    capabilities = {**Adapter.capabilities, "login": True, "vendor_transpilation": True}

    def client(self):
        if not hasattr(self, "_workspace"):
            self._workspace = self.config.get("_client")
            if self._workspace is None:
                workspace_type = optional("qdk.azure", "azure").Workspace
                if self.config.get("connection_string"):
                    self._workspace = workspace_type.from_connection_string(self.config["connection_string"])
                    return self._workspace
                identity = optional("azure.identity", "azure")
                credential = self.config.get("_credential")
                if credential is None:
                    if self.config.get("client_secret"):
                        credential = identity.ClientSecretCredential(self.need("tenant_id"), self.need("client_id"), self.need("client_secret"))
                    else:
                        credential = identity.DefaultAzureCredential()
                self._workspace = workspace_type(
                    resource_id=self.need("workspace_resource_id", "resource", "resource_id"), credential=credential)
        return self._workspace

    def login(self):
        credential = optional("azure.identity", "azure").InteractiveBrowserCredential()
        credential.authenticate(scopes=["https://quantum.microsoft.com/.default"])
        self.config["_credential"] = credential
        return {"route": self.route, "authenticated": True, "method": "Azure browser credential",
                "message": "Browser credential belongs to this process; use Azure CLI or service principal for later commands."}

    def discover(self):
        targets = self.client().get_targets()
        records = []
        # Azure's target API lists account availability but omits gates/topology.
        # Augment only exact, versioned official provider contracts (2026-10-09).
        h2 = {f"quantinuum.{kind}.h2-{n}{suffix}": (32 if suffix == "e" else 56)
              for n in (1, 2) for kind, suffix in (("qpu", ""), ("sim", "e"), ("sim", "sc"))}
        ionq = {"ionq.qpu.aria-1": (25, "ms"), "ionq.qpu.forte-1": (36, "rzz"),
                "ionq.qpu.forte-enterprise-1": (36, "rzz")}
        for target in targets:
            name = target.name
            record = {"route": self.route, "device": name, "provider": target.provider_id,
                      "vendor": str(target.provider_id).lower(),
                      "formats": [target.input_data_format], "capability_verified": False,
                      "raw": {"input_data_format": target.input_data_format, "output_data_format": target.output_data_format}}
            if name in h2 or name in ionq:
                is_h2 = name in h2
                record.update(qubits=h2[name] if is_h2 else ionq[name][0],
                    qir_platform="quantinuum-h2" if is_h2 else "standard",
                    native_gates=["u1q", "rz", "rzz", "measure", "reset"] if is_h2 else ["gpi", "gpi2", ionq[name][1], "measure"],
                    coupling=[], all_to_all=True, directed_connectivity=False, parameter_units="radians",
                    supports={"reset": is_h2, "mid_circuit_measure": is_h2, "feedforward": is_h2},
                    formats=["qir-bitcode", "openqasm2"] if is_h2 else ["ionq-native-json"],
                    capability_verified=True, capability_contract_date="2026-10-09",
                    execution_kind="syntax-checker" if name.endswith("sc") else ("emulator" if name.endswith("e") and is_h2 else "qpu"),
                    capability_sources=["https://learn.microsoft.com/en-us/azure/quantum/provider-" + ("quantinuum" if is_h2 else "ionq"),
                        "https://docs.quantinuum.com/nexus/trainings/notebooks/basics/qir/index.html" if is_h2 else "https://docs.ionq.com/features/getting-started-with-native-gates"])
            records.append(record)
        return records

    def submit(self, artifact, options):
        self.validate(artifact, options, {"qir-bitcode", "qir.bc", "qir.v1", "openqasm2", "qasm2", "ionq-json", "ionq-native-json"})
        target_name = self.config.get("device") or artifact.target
        target = self.client().get_targets(name=target_name)
        fmt = artifact.format.lower()
        params = dict(self.config.get("input_params", {}))
        if str(target.provider_id).lower() == "quantinuum":
            params.update({"no-opt": True, "noreduce": True})
        elif str(target.provider_id).lower() == "ionq":
            # IonQ's default debiasing changes decompositions and qubit assignments.
            params["error-mitigation"] = {"debias": False}
        if fmt in {"qir-bitcode", "qir.bc", "qir.v1"}:
            data = artifact.program_bytes()
            kwargs = {"input_data_format": "qir.v1", "content_type": "qir.v1"}
            entry_point = artifact.manifest.get("qir_entry_point", "main")
            if self.config.get("entry_point", entry_point) != entry_point or params.get("entryPoint", entry_point) != entry_point:
                raise QStackError("Azure entryPoint must match the compiled QIR entry point", "INVALID_CONFIG")
            params["entryPoint"] = entry_point
            params.setdefault("arguments", [])
        elif fmt in {"openqasm2", "qasm2"}:
            if not str(target.provider_id).lower().startswith("quantinuum"):
                raise QStackError("Azure QASM2 route is only defined for Quantinuum targets")
            data, kwargs = artifact.program_text(), {"input_data_format": target.input_data_format, "content_type": "application/qasm"}
        else:
            if str(target.provider_id).lower() != "ionq":
                raise QStackError("Azure IonQ JSON requires an IonQ target")
            import json
            data, kwargs = json.loads(artifact.program_text()), {"input_data_format": "ionq.circuit.v1", "content_type": "application/json"}
        job = target.submit(input_data=data, name=options.name, shots=options.shots, input_params=params, **kwargs)
        return self.receipt(artifact, job.id, device=target_name, provider=target.provider_id)

    def status(self, receipt):
        job = self.client().get_job(receipt.job_id)
        job.refresh()
        return {"job_id": receipt.job_id, "status": plain(job.details.status), "details": plain(job.details)}

    def results(self, receipt):
        raw = self.client().get_job(receipt.job_id).get_results()
        return self.result(receipt, raw, result_kind="provider-native")

    def cancel(self, receipt):
        self.client().cancel_job(self.client().get_job(receipt.job_id))
        return {"job_id": receipt.job_id, "cancellation_requested": True}
