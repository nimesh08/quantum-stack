"""Independent channel/instrument checks of public source-to-physical compilation.

The reference uses elementary ket definitions, not compiler matrix helpers or its
simulator. A normalized maximally entangled system/reference state determines
the complete finite-dimensional Choi operator for every classical outcome. We
compare its unnormalized density matrix, so both outcome probability and the
corresponding post-measurement state (including reference correlations) matter.
This is finite numerical evidence, not a formal proof for arbitrary programs.
All floating comparisons use an absolute 2e-10 tolerance; random seed 0x51C1E.
"""
from __future__ import annotations

import cmath
import math
import random
from pathlib import Path

import pytest

from qstack.models import QStackError
from qstack.registry import cache_targets
from qstack.service import compile_file, find_binary


TOLERANCE = 2e-10
SEED = 0x51C1E


def op(name, *qubits, clbit=None, angle=None, value=1):
    result = {"op": name, "qubits": list(qubits), "clbits": [] if clbit is None else [clbit],
              "params": [] if angle is None else [angle]}
    if name == "if":
        result["condition_value"] = value
    return result


def one_qubit_matrix(instruction):
    name = instruction["op"]
    a = instruction.get("params", [0])[0] if instruction.get("params") else 0
    if name == "x":
        return ((0, 1), (1, 0))
    if name == "y":
        return ((0, -1j), (1j, 0))
    if name == "z":
        return ((1, 0), (0, -1))
    if name == "h":
        t = math.sqrt(0.5)
        return ((t, t), (t, -t))
    if name == "sx":
        return (((1 + 1j) / 2, (1 - 1j) / 2), ((1 - 1j) / 2, (1 + 1j) / 2))
    if name == "rz":
        return ((cmath.exp(-0.5j * a), 0), (0, cmath.exp(0.5j * a)))
    if name == "rx":
        c, s = math.cos(a / 2), -1j * math.sin(a / 2)
        return ((c, s), (s, c))
    if name == "ry":
        c, s = math.cos(a / 2), math.sin(a / 2)
        return ((c, -s), (s, c))
    raise AssertionError(f"Independent reference has no definition for {name}")


def unitary(vector, instruction, positions):
    """Apply little-endian ket definitions; first CX operand is the control."""
    name = instruction["op"]
    wires = [positions[q] for q in instruction["qubits"]]
    result = list(vector)
    if name in {"barrier", "gphase"}:
        # A phase common to the entire trajectory cancels in its density matrix.
        return result
    if name in {"cx", "cz", "swap"}:
        a, b = (1 << q for q in wires)
        result = [0j] * len(vector)
        for index, amplitude in enumerate(vector):
            destination = index
            if name == "cx" and index & a:
                destination ^= b
            if name == "swap" and bool(index & a) != bool(index & b):
                destination ^= a | b
            result[destination] += -amplitude if name == "cz" and index & a and index & b else amplitude
        return result
    matrix = one_qubit_matrix(instruction)
    mask = 1 << wires[0]
    for index in range(len(vector)):
        if index & mask:
            continue
        other = index | mask
        result[index] = matrix[0][0] * vector[index] + matrix[0][1] * vector[other]
        result[other] = matrix[1][0] * vector[index] + matrix[1][1] * vector[other]
    return result


def instrument(physical, logical_width):
    """Return unnormalized Choi trajectories grouped by recorded classical data."""
    initial = physical.get("initial_logical_to_physical", list(range(logical_width)))
    final = physical.get("logical_to_physical", list(range(logical_width)))
    active = sorted(set(initial) | set(final) |
                    {q for instruction in physical["instructions"] for q in instruction["qubits"]})
    assert len(active) == logical_width, "This reference intentionally excludes extra routing ancillas"
    positions = {wire: index for index, wire in enumerate(active)}
    vector = [0j] * (1 << (2 * logical_width))
    for basis in range(1 << logical_width):
        system = sum(((basis >> q) & 1) << positions[initial[q]] for q in range(logical_width))
        vector[system | (basis << logical_width)] = 2 ** (-logical_width / 2)
    instructions = physical["instructions"]

    def execute(start, stop, branches):
        index = start
        while index < stop:
            instruction = instructions[index]
            name = instruction["op"]
            if name == "if":
                depth, end, alternative = 1, index + 1, None
                while depth:
                    marker = instructions[end]["op"]
                    if marker == "if":
                        depth += 1
                    elif marker == "endif":
                        depth -= 1
                    elif marker == "else" and depth == 1:
                        alternative = end
                    end += 1
                end -= 1
                next_branches = []
                for bits, state in branches:
                    take = bits[instruction["clbits"][0]] == instruction["condition_value"]
                    lo, hi = (index + 1, alternative if alternative is not None else end) if take else (
                        alternative + 1 if alternative is not None else end, end)
                    next_branches.extend(execute(lo, hi, [(bits, state)]))
                branches, index = next_branches, end + 1
                continue
            assert name not in {"else", "endif"}
            next_branches = []
            for bits, state in branches:
                if name in {"measure", "reset"}:
                    mask = 1 << positions[instruction["qubits"][0]]
                    for outcome in (0, 1):
                        projected = [amplitude if bool(k & mask) == bool(outcome) else 0j
                                     for k, amplitude in enumerate(state)]
                        if not any(abs(amplitude) > 1e-15 for amplitude in projected):
                            continue
                        updated = list(bits)
                        if name == "measure":
                            updated[instruction["clbits"][0]] = outcome
                        elif outcome:
                            projected = unitary(projected, op("x", instruction["qubits"][0]), positions)
                        next_branches.append((tuple(updated), projected))
                else:
                    next_branches.append((bits, unitary(state, instruction, positions)))
            branches = next_branches
            index += 1
        return branches

    branches = execute(0, len(instructions), [((0,) * physical["num_clbits"], vector)])
    grouped = {}
    for bits, state in branches:
        canonical = [0j] * len(state)
        for old, amplitude in enumerate(state):
            system = sum(((old >> positions[final[q]]) & 1) << q for q in range(logical_width))
            canonical[system | ((old >> logical_width) << logical_width)] = amplitude
        grouped.setdefault(bits, []).append(canonical)
    return grouped


def assert_instruments_equal(actual, expected):
    assert set(actual) == set(expected)
    for outcome in actual:
        lhs, rhs = actual[outcome], expected[outcome]
        probability = lambda vectors: sum(abs(a) ** 2 for vector in vectors for a in vector)
        assert probability(lhs) == pytest.approx(probability(rhs), abs=TOLERANCE)
        size = len(lhs[0])
        maximum = 0.0
        for row in range(size):
            for column in range(size):
                a = sum(vector[row] * vector[column].conjugate() for vector in lhs)
                b = sum(vector[row] * vector[column].conjugate() for vector in rhs)
                maximum = max(maximum, abs(a - b))
        assert maximum <= TOLERANCE, (outcome, maximum)


def expected_ir(width, bits, instructions):
    return {"num_qubits": width, "num_clbits": bits, "instructions": instructions}


def compile_program(source, extension, width, tmp_path, monkeypatch, level):
    try:
        find_binary("photonc")
        find_binary("phononc")
    except QStackError:
        pytest.skip("C++ compiler binaries are required for source semantic evidence")
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path / "state"))
    snapshot = {"route": "ibm", "vendor": "ibm", "device": "instrument-fixture", "qubits": width,
                "native_gates": ["rz", "sx", "x", "cz"], "all_to_all": True, "coupling": [],
                "formats": ["qiskit-native"], "capability_verified": True,
                "capability_sources": ["independent mathematical audit fixture; no hardware claim"],
                "parameter_units": "radians",
                "supports": {"reset": True, "feedforward": True, "mid_circuit_measure": True}}
    cache_targets("ibm", [snapshot])
    path = tmp_path / ("instrument." + extension)
    path.write_text(source, encoding="utf-8")
    return compile_file(path, target=snapshot["device"], config={"provider": "ibm"},
                        optimization_level=level).physical_ir


@pytest.mark.parametrize("level", range(4))
def test_reset_feedforward_and_nonsequential_readout_preserve_complete_instrument(tmp_path, monkeypatch, level):
    source = """target generic
qubit q[3]
bit c[3]
ry(0.37) q[0]
cx q[0], q[1]
c[2] = measure q[0]
reset q[0]
if (c[2] == 1) {
  rz(0.71) q[1]
} else {
  h q[2]
}
cx q[0], q[2]
c[0] = measure q[1]
if (c[0] != 1) {
  x q[2]
} else {
  z q[2]
}
"""
    expected = expected_ir(3, 3, [op("ry", 0, angle=.37), op("cx", 0, 1), op("measure", 0, clbit=2),
        op("reset", 0), op("if", clbit=2), op("rz", 1, angle=.71), op("else"), op("h", 2), op("endif"),
        op("cx", 0, 2), op("measure", 1, clbit=0), op("if", clbit=0, value=0), op("x", 2),
        op("else"), op("z", 2), op("endif")])
    compiled = compile_program(source, "phn", 3, tmp_path, monkeypatch, level)
    assert_instruments_equal(instrument(compiled, 3), instrument(expected, 3))


@pytest.mark.parametrize("level", range(4))
@pytest.mark.parametrize("specialized", [False, True])
def test_conditional_return_cycle_preserves_complete_instrument(tmp_path, monkeypatch, level, specialized):
    parameter = ", int count" if specialized else ""
    argument = ", 1" if specialized else ""
    source = f"""target generic
def cycle(qubit a, qubit b, qubit d{parameter}) {{
  ry(0.43) a
  return b, d, a
}}
qubit q[4]
bit c[4]
c[3] = measure q[3]
if (c[3] == 0) {{
  cycle(q[0], q[1], q[2]{argument})
}} else {{
  if (c[3] == 1) {{
    z q[1]
  }}
}}
"""
    expected = expected_ir(4, 4, [op("measure", 3, clbit=3), op("if", clbit=3, value=0),
        op("ry", 0, angle=.43), op("swap", 0, 1), op("swap", 1, 2), op("else"),
        op("if", clbit=3), op("z", 1), op("endif"), op("endif")])
    compiled = compile_program(source, "phn", 4, tmp_path, monkeypatch, level)
    assert_instruments_equal(instrument(compiled, 4), instrument(expected, 4))


@pytest.mark.parametrize("level", range(4))
def test_photon_indexed_loops_and_both_measurement_branches_preserve_instrument(tmp_path, monkeypatch, level):
    source = """target generic
kernel branch() {
  QReg q(3)
  for i in 0..3 {
    q.rx(pi * (i + 1) / 7, i)
  }
  Bit flag = q.measure(0)
  if (flag >= 1) {
    q.x(1)
  } else {
    q.z(2)
  }
}
"""
    expected = expected_ir(3, 3, [*[op("rx", i, angle=math.pi * (i + 1) / 7) for i in range(3)],
        op("measure", 0, clbit=0), op("if", clbit=0), op("x", 1), op("else"), op("z", 2), op("endif")])
    compiled = compile_program(source, "pho", 3, tmp_path, monkeypatch, level)
    assert_instruments_equal(instrument(compiled, 3), instrument(expected, 3))


@pytest.mark.parametrize("level", range(4))
def test_bound_function_indices_angles_and_while_preserve_instrument(tmp_path, monkeypatch, level):
    source = """target generic
qubit q[3]
bit c[3]
int slot = 0
def turn(int repetitions, angle step) {
  int i = 0
  while (i < repetitions) {
    rz(step * (i + 1)) q[slot]
    i = i + 1
  }
}
slot = 1
turn(2, 0.125)
slot = 2
turn(1, 0.5)
c[2] = measure q[0]
if (c[2] == 1) {
  cx q[1], q[2]
}
"""
    expected = expected_ir(3, 3, [op("rz", 1, angle=.125), op("rz", 1, angle=.25),
        op("rz", 2, angle=.5), op("measure", 0, clbit=2), op("if", clbit=2), op("cx", 1, 2), op("endif")])
    compiled = compile_program(source, "phn", 3, tmp_path, monkeypatch, level)
    assert_instruments_equal(instrument(compiled, 3), instrument(expected, 3))


@pytest.mark.parametrize("level", range(4))
def test_photon_impossible_bit_predicate_preserves_else_and_following_state(tmp_path, monkeypatch, level):
    source = """target generic
kernel impossible() {
  QReg q(2)
  Bit flag = q.measure(0)
  if (flag == -1) {
    q.h(1)
  } else {
    q.x(1)
  }
  q.rz(0.23, 1)
}
"""
    expected = expected_ir(2, 2, [op("measure", 0, clbit=0), op("x", 1), op("rz", 1, angle=.23)])
    compiled = compile_program(source, "pho", 2, tmp_path, monkeypatch, level)
    assert_instruments_equal(instrument(compiled, 2), instrument(expected, 2))


@pytest.mark.parametrize("level", range(4))
def test_projected_state_reuse_and_overwritten_branch_flag_preserve_instrument(tmp_path, monkeypatch, level):
    source = """target generic
qubit q[3]
bit c[3]
h q[0]
cx q[0], q[1]
c[2] = measure q[1]
if (c[2] == 1) {
  cx q[0], q[2]
  reset q[1]
  c[2] = measure q[2]
} else {
  cx q[1], q[2]
  ry(0.43) q[0]
}
cx q[0], q[2]
c[0] = measure q[0]
"""
    expected = expected_ir(3, 3, [op("h", 0), op("cx", 0, 1), op("measure", 1, clbit=2),
        op("if", clbit=2), op("cx", 0, 2), op("reset", 1), op("measure", 2, clbit=2), op("else"),
        op("cx", 1, 2), op("ry", 0, angle=.43), op("endif"), op("cx", 0, 2), op("measure", 0, clbit=0)])
    compiled = compile_program(source, "phn", 3, tmp_path, monkeypatch, level)
    assert_instruments_equal(instrument(compiled, 3), instrument(expected, 3))


@pytest.mark.parametrize("level", range(4))
def test_repeated_measurement_then_gate_has_correct_entangled_channel(tmp_path, monkeypatch, level):
    source = """target generic
qubit q[2]
bit c[3]
cx q[0], q[1]
c[0] = measure q[0]
c[1] = measure q[0]
x q[0]
c[2] = measure q[0]
"""
    expected = expected_ir(2, 3, [op("cx", 0, 1), op("measure", 0, clbit=0), op("measure", 0, clbit=1),
        op("x", 0), op("measure", 0, clbit=2)])
    compiled = compile_program(source, "phn", 2, tmp_path, monkeypatch, level)
    actual = instrument(compiled, 2)
    assert all(bits[0] == bits[1] != bits[2] for bits in actual)
    assert_instruments_equal(actual, instrument(expected, 2))


@pytest.mark.parametrize("level", range(4))
def test_photon_reset_and_saved_flags_preserve_instrument(tmp_path, monkeypatch, level):
    source = """target generic
kernel saved() {
  QReg q(2)
  Bit first = q.measure(0)
  q.reset(0)
  Bit second = q.measure(0)
  if (first == 1) {
    q.x(1)
  }
  if (second == 1) {
    q.z(1)
  }
}
"""
    expected = expected_ir(2, 3, [op("measure", 0, clbit=0), op("reset", 0), op("measure", 0, clbit=2),
        op("if", clbit=0), op("x", 1), op("endif"), op("if", clbit=2), op("z", 1), op("endif")])
    compiled = compile_program(source, "pho", 2, tmp_path, monkeypatch, level)
    assert_instruments_equal(instrument(compiled, 2), instrument(expected, 2))


@pytest.mark.parametrize("level", range(4))
def test_python_reset_and_saved_flags_preserve_instrument(tmp_path, monkeypatch, level):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[4] / "photon" / "frontends" / "python"))
    from photon import QReg
    from photon._translator import translate
    def saved():
        q = QReg(2)
        first = q.measure()
        q.reset(0)
        second = q.measure()
        if first[0] == 1:
            q.x(1)
        if second[0] == 1:
            q.z(1)
    source = translate(saved)
    expected = expected_ir(2, 4, [op("measure", 0, clbit=0), op("measure", 1, clbit=1), op("reset", 0),
        op("measure", 0, clbit=2), op("measure", 1, clbit=3), op("if", clbit=0), op("x", 1), op("endif"),
        op("if", clbit=2), op("z", 1), op("endif")])
    compiled = compile_program(source, "phn", 2, tmp_path, monkeypatch, level)
    assert_instruments_equal(instrument(compiled, 2), instrument(expected, 2))


def test_python_return_register_excludes_saved_internal_flags(tmp_path, monkeypatch):
    monkeypatch.syspath_prepend(str(Path(__file__).resolve().parents[4] / "photon" / "frontends" / "python"))
    import photon
    # The public execution path is qstack; no extension presence is required.
    monkeypatch.setattr(photon, "_engine", None)
    @photon.kernel(target="instrument-fixture")
    def returned():
        q = photon.QReg(1)
        first = q.measure()
        q.x(0)
        saved = q.measure()
        q.reset(0)
        return q.measure_int()
    # Establish the offline target with the same public compilation path.
    compiled = compile_program(returned.phonon_text, "phn", 1, tmp_path, monkeypatch, 2)
    assert compiled["num_clbits"] == 2
    from qstack import run_source
    raw = run_source(returned.phonon_text, language="phonon", target="instrument-fixture",
                     mode="local", shots=16, provider="ibm")
    assert raw.counts == {"10": 16}
    assert returned.run(shots=16, provider="ibm") == {"0": 16}


@pytest.mark.parametrize("level", range(4))
def test_seeded_adversarial_measurement_predicates_preserve_instrument(tmp_path, monkeypatch, level):
    rng = random.Random(SEED)
    for predicate, threshold in [("<", .5), (">=", .5), ("!=", 2), ("==", -1), ("==", 1e-30)]:
        angle = rng.uniform(-math.pi, math.pi)
        source = f"""target generic
qubit q[2]
bit c[2]
ry({angle!r}) q[0]
c[1] = measure q[0]
if (c[1] {predicate} {threshold!r}) {{
  h q[1]
}} else {{
  x q[1]
}}
"""
        truth = lambda bit: {"<": bit < threshold, ">=": bit >= threshold,
                             "!=": bit != threshold, "==": bit == threshold}[predicate]
        start = [op("ry", 0, angle=angle), op("measure", 0, clbit=1)]
        if truth(0) == truth(1):
            start.append(op("h" if truth(0) else "x", 1))
        else:
            start += [op("if", clbit=1, value=int(truth(1))), op("h", 1), op("else"), op("x", 1), op("endif")]
        compiled = compile_program(source, "phn", 2, tmp_path, monkeypatch, level)
        assert_instruments_equal(instrument(compiled, 2), instrument(expected_ir(2, 2, start), 2))
