"""Normalize supplied numerical calibrations; never fill missing values."""
from __future__ import annotations
from math import isfinite


def finite_number(value, low=0, high=None):
    return isinstance(value, (int, float)) and not isinstance(value, bool) and isfinite(value) and value >= low and (high is None or value <= high)


class Calibrations:
    def __init__(self):
        self.single, self.readout, self.pair, self.durations = {}, {}, {}, []

    def add_error(self, op, qubits, error):
        if not finite_number(error, high=1):
            return
        if len(qubits) == 1:
            collection = self.readout if op == "measure" else self.single
        elif len(qubits) == 2:
            collection = self.pair
        else:
            return
        key = tuple(qubits)
        collection[key] = max(error, collection.get(key, 0))

    def add_duration(self, op, qubits, duration_ns):
        if qubits and finite_number(duration_ns):
            self.durations.append({"op": op, "qubits": list(qubits), "duration_ns": duration_ns})

    def fields(self):
        return {"one_qubit_errors": [[*q, value] for q, value in sorted(self.single.items())],
                "readout_errors": [[*q, value] for q, value in sorted(self.readout.items())],
                "two_qubit_errors": [[*q, value] for q, value in sorted(self.pair.items())],
                "instruction_durations": self.durations,
                "calibration_aggregation": "maximum reported native-operation error for each locus; missing values omitted"}


def rigetti_calibrations(isa):
    data = Calibrations()
    fidelity_names = {"CZ": "fCZ", "ISWAP": "fISWAP", "CPHASE": "fCPHASE", "XY": "fXY", "MEASURE": "fRO"}
    for instruction in isa.instructions:
        expected = fidelity_names.get(instruction.name)
        if expected is None:
            continue
        for site in instruction.sites:
            for characteristic in getattr(site, "characteristics", []):
                if characteristic.name == expected and finite_number(characteristic.value, high=1):
                    # Characteristic.error is uncertainty, not operation infidelity.
                    qubits = getattr(characteristic, "node_ids", None) or site.node_ids
                    op = "measure" if instruction.name == "MEASURE" else instruction.name.lower()
                    data.add_error(op, qubits, 1 - characteristic.value)
    for benchmark in getattr(isa, "benchmarks", []):
        if benchmark.name != "randomized_benchmark_simultaneous_1q":
            continue
        for site in benchmark.sites:
            for characteristic in getattr(site, "characteristics", []):
                qubits = getattr(characteristic, "node_ids", None) or site.node_ids
                if len(qubits) == 1 and finite_number(characteristic.value, high=1):
                    data.add_error("rx", qubits, 1 - characteristic.value)
    # QCS ISA characteristics do not document timing units. pyQuil's built-in
    # timing constants are guesses; they are deliberately not calibration data.
    return data.fields()
