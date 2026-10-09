"""Calibrated Qibolab execution with a built-in native pulse assembler.

The optional custom ``qibolab_bridge='module:factory'`` remains available for
laboratory extensions outside the supported native gate subset.
"""
from __future__ import annotations

from importlib import import_module
from uuid import uuid4
from qstack.models import QStackError
from .base import Adapter, optional, plain


class QibolabAdapter(Adapter):
    route = "qibolab"
    capabilities = {**Adapter.capabilities, "cancel": False, "login": False,
                    "synchronous": True, "requires_configured_lab": True}

    def bridge(self):
        if not hasattr(self, "_bridge"):
            bridge = self.config.get("_bridge")
            if bridge is None and (self.config.get("qibolab_platform") or self.config.get("_platform")):
                from .qibolab_native import load_platform, platform_snapshot
                platform = self.config.get("_platform") or load_platform(self.config["qibolab_platform"])
                bridge = {"platform": platform, "snapshot": platform_snapshot(platform), "native": True}
            if bridge is None:
                factory_path = self.need("qibolab_bridge")
                module_name, separator, function_name = factory_path.partition(":")
                if not separator or not module_name or not function_name.isidentifier():
                    raise QStackError("qibolab_bridge must be an installed 'module:factory'", "INVALID_CONFIG")
                bridge = getattr(import_module(module_name), function_name)(dict(self.config))
            if not isinstance(bridge, dict) or not all(key in bridge for key in ("platform", "snapshot")):
                raise QStackError("Qibolab bridge must provide platform, snapshot, and build_sequences")
            if not bridge.get("native") and not callable(bridge.get("build_sequences")):
                raise QStackError("Qibolab bridge build_sequences must be callable")
            self._bridge = bridge
        return self._bridge

    def check_auth(self):
        self.bridge()
        return {"route": self.route, "authenticated": None, "configured": True,
                "message": "Configured local laboratory platform; no cloud authentication applies."}

    def login(self):
        return self.check_auth()

    def discover(self):
        snapshot = dict(self.bridge()["snapshot"])
        snapshot.update(route=self.route, formats=["qibolab-native"])
        # The trusted laboratory owns the calibration and capability attestation.
        snapshot.setdefault("capability_verified", False)
        snapshot.setdefault("capability_sources", ["configured laboratory bridge"])
        return [snapshot]

    def submit(self, artifact, options):
        self.validate(artifact, options)
        bridge = self.bridge()
        acquisitions = []
        if bridge.get("native"):
            from .qibolab_native import build_native_sequences
            sequences, acquisitions = build_native_sequences(bridge["platform"], artifact)
        else:
            sequences = bridge["build_sequences"](artifact)
        if not isinstance(sequences, list) or not sequences:
            raise QStackError("Qibolab bridge must return a nonempty list of calibrated PulseSequences")
        platform = bridge["platform"]
        sdk = optional("qibolab", "qibolab")
        platform.connect()
        try:
            raw = platform.execute(sequences, nshots=options.shots,
                                   acquisition_type=sdk.AcquisitionType.DISCRIMINATION,
                                   averaging_mode=sdk.AveragingMode.SINGLESHOT)
        finally:
            platform.disconnect()
        # Qibolab is synchronous and has no remote job service. Persist actual returned
        # data in the receipt so status/results can be resumed from another process.
        return self.receipt(artifact, "qibolab-" + uuid4().hex,
                            execution_kind="synchronous-lab", completed_result=plain(raw),
                            acquisition_mapping=acquisitions, shots=options.shots)

    def status(self, receipt):
        if receipt.metadata.get("execution_kind") != "synchronous-lab" or "completed_result" not in receipt.metadata:
            raise QStackError("Receipt does not contain a completed Qibolab execution")
        return {"job_id": receipt.job_id, "status": "COMPLETED"}

    def results(self, receipt):
        self.status(receipt)
        from .qibolab_native import acquisition_counts
        raw = receipt.metadata["completed_result"]
        counts = acquisition_counts(raw, receipt.metadata.get("acquisition_mapping", []),
            receipt.metadata.get("num_clbits", 0), receipt.metadata.get("shots", 0))
        return self.result(receipt, raw, counts, result_kind="acquisition-id-arrays", bit_order="classical-msb-first")

    def cancel(self, receipt):
        self.unsupported("cancel", "Qibolab execution is synchronous; there is no queued cloud job to cancel")
