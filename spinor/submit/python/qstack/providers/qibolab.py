"""Explicit laboratory bridge for calibrated Qibolab pulse execution.

A lab supplies ``qibolab_bridge='module:factory'``. The factory receives resolved
config and returns ``platform``, ``snapshot`` and ``build_sequences(artifact)``.
This avoids inventing calibration parameters or invoking Qibo's transpiler.
"""
from __future__ import annotations

from importlib import import_module
from uuid import uuid4
from qstack.models import QStackError
from .base import Adapter, optional, plain


class QibolabAdapter(Adapter):
    route = "qibolab"
    capabilities = {**Adapter.capabilities, "cancel": False, "login": False,
                    "synchronous": True, "requires_configured_lab_bridge": True}

    def bridge(self):
        if not hasattr(self, "_bridge"):
            bridge = self.config.get("_bridge")
            if bridge is None:
                factory_path = self.need("qibolab_bridge")
                module_name, separator, function_name = factory_path.partition(":")
                if not separator or not module_name or not function_name.isidentifier():
                    raise QStackError("qibolab_bridge must be an installed 'module:factory'", "INVALID_CONFIG")
                bridge = getattr(import_module(module_name), function_name)(dict(self.config))
            if not isinstance(bridge, dict) or not all(key in bridge for key in ("platform", "snapshot", "build_sequences")):
                raise QStackError("Qibolab bridge must provide platform, snapshot, and build_sequences")
            if not callable(bridge["build_sequences"]):
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
                            execution_kind="synchronous-lab", completed_result=plain(raw))

    def status(self, receipt):
        if receipt.metadata.get("execution_kind") != "synchronous-lab" or "completed_result" not in receipt.metadata:
            raise QStackError("Receipt does not contain a completed Qibolab execution")
        return {"job_id": receipt.job_id, "status": "COMPLETED"}

    def results(self, receipt):
        self.status(receipt)
        return self.result(receipt, receipt.metadata["completed_result"], result_kind="acquisition-id-arrays")

    def cancel(self, receipt):
        self.unsupported("cancel", "Qibolab execution is synchronous; there is no queued cloud job to cancel")
