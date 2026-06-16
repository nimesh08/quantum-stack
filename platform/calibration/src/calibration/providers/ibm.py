"""IBM provider — uses qiskit-ibm-runtime if available; otherwise
raises so the writer keeps the previous file (D5)."""

from __future__ import annotations

from typing import Any

from .base import CalibrationProvider


class IbmProvider(CalibrationProvider):
    name = "ibm"

    def fetch(self, chip_id: str) -> dict[str, Any]:
        try:
            from qiskit_ibm_runtime import QiskitRuntimeService  # type: ignore
        except ImportError as exc:  # pragma: no cover
            raise RuntimeError(
                "qiskit-ibm-runtime is not installed; cannot fetch live IBM "
                "calibration. Install it or use the fixture provider."
            ) from exc

        service = QiskitRuntimeService()
        backend = service.backend(chip_id)
        props = backend.properties()
        return {
            "chip_id": chip_id,
            "source": "ibm_runtime_api",
            "backend_version": getattr(props, "backend_version", ""),
            "single_qubit_errors": {
                str(q): float(props.gate_error("sx", [q]) or 0)
                for q in range(backend.num_qubits)
            },
            "two_qubit_errors": {
                f"{a}-{b}": float(props.gate_error("ecr", [a, b]) or 0)
                for (a, b) in backend.coupling_map.get_edges()
            },
            "readout_errors": {
                str(q): float(props.readout_error(q) or 0)
                for q in range(backend.num_qubits)
            },
            "t1_us": {
                str(q): float(props.t1(q) * 1e6) if props.t1(q) else 0
                for q in range(backend.num_qubits)
            },
            "t2_us": {
                str(q): float(props.t2(q) * 1e6) if props.t2(q) else 0
                for q in range(backend.num_qubits)
            },
        }


__all__ = ["IbmProvider"]
