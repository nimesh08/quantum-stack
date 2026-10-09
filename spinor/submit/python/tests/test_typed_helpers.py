"""Typed helper semantics, including independent reference-entangled instruments.

Expected operators below use basis-index arithmetic, without compiler gate
helpers, the production serializer, or the C++ simulator as their expectation.
"""
import copy

import numpy as np
import pytest

from qstack.models import QStackError, SubmissionOptions
from qstack.service import submit_artifact
from qstack.verification.engine import execute
from test_runtime_classical import runtime_target


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
@pytest.mark.parametrize("flag", [0, 1])
def test_typed_tuple_calls_keep_snapshots_widths_and_separate_invocations(runtime_target, fmt, flag):
    source = """def choose(bool flag, uint[64] n) -> (uint[64], bool) {
if (flag) {
return n + 2, !flag
}
return n - 1, !flag
}
qubit q[1]
bit c[1]
""" + ("x q[0]\n" if flag else "") + """
c[0] = measure q[0]
bool saved = c[0]
reset q[0]
c[0] = measure q[0]
uint[64] n = 9007199254740993
bool opposite = false
n, opposite = choose(saved, n)
uint[64] first = n
n, opposite = choose(opposite, n)
output saved
output first
output n
output opposite
"""
    artifact = runtime_target(source, fmt)
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=4), wait=True)
    assert result.metadata["classical_counts"] == {
        "saved": {str(flag): 4},
        "first": {str(9007199254740995 if flag else 9007199254740992): 4},
        "n": {"9007199254740994": 4},
        "opposite": {str(flag): 4},
    }


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
@pytest.mark.parametrize("level", [0, 2, 3])
def test_mixed_quantum_returns_preserve_the_complete_instrument(runtime_target, fmt, level):
    artifact = runtime_target("""def choose(qubit a, qubit b, bool flag, uint[8] n) -> (qubit, qubit, uint[8]) {
if (flag) {
x a
return b, a, n + 1
reset b
}
z b
return a, b, n - 1
reset a
}
qubit q[3]
bit c[1]
c[0] = measure q[0]
uint[8] result = choose(q[1], q[2], c[0], uint[8](7))
output result
""", fmt, level=level)

    # Keep the public measured flag and typed result, while ignoring private
    # return guards. These definitions do not depend on their storage layout.
    physical = copy.deepcopy(artifact.physical_ir)
    physical["exported_clbits"] = [0]
    physical["classical_outputs"] = [value for value in physical["classical_outputs"]
                                     if value["name"] == "result"]
    _, actual = execute(physical, max_qubits=3, max_paths=32)
    expected = {}
    for flag in (0, 1):
        operator = np.zeros((8, 8), dtype=complex)
        for column in range(8):
            if column & 1 != flag:
                continue
            row, phase = column, 1
            if flag:
                row ^= 2  # X on logical a.
                if bool(row & 2) != bool(row & 4):
                    row ^= 6  # The returned b,a states occupy fixed a,b wires.
            elif column & 4:
                phase = -1  # Z on b in the non-returning first branch.
            operator[row, column] = phase
        vector = operator.reshape(-1)
        expected[((flag,), (("result", "uint", 8, 8 if flag else 6),))] = np.outer(vector, vector.conj())
    assert set(actual) == set(expected)
    for key in expected:
        np.testing.assert_allclose(actual[key], expected[key], atol=2e-10, rtol=0)


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
def test_nested_typed_calls_do_not_consume_the_callers_return_state(runtime_target, fmt):
    artifact = runtime_target("""def choose(bool flag, uint[8] n) -> uint[8] {
if (flag) {
return n + 2
}
return n - 1
}
def combine(bool flag, uint[8] n) -> uint[8] {
if (flag) {
return choose(flag, n) + choose(!flag, n)
}
return choose(!flag, n) - choose(flag, n)
}
qubit q[1]
bit c[1]
h q[0]
c[0] = measure q[0]
uint[8] result = combine(c[0], uint[8](127))
output result
""", fmt)
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=128), wait=True)
    assert set(result.metadata["classical_counts"]["result"]) == {"3", "255"}
    assert sum(result.metadata["classical_counts"]["result"].values()) == 128


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
def test_typed_early_return_inside_bounded_loop_preserves_exhaustion(runtime_target, fmt):
    artifact = runtime_target("""def count(bool stop) -> uint[8] {
uint[8] n = 0
while (n < 3) max_iterations 2 {
n = n + 1
if (stop) {
return n
}
}
return n + 10
}
qubit q[1]
bit c[1]
h q[0]
c[0] = measure q[0]
uint[8] result = count(c[0])
output result
""", fmt)
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=128), wait=True)
    values = result.metadata["classical_counts"]["result"]
    assert set(values) == {"1", "12"}
    assert sum(values.values()) == 128
    assert result.metadata["application_status"] == "loop_exhausted"
    flags = [counts for name, counts in result.metadata["classical_counts"].items()
             if name.startswith("loop_exhausted_")]
    assert len(flags) == 1
    assert flags[0] == {"0": values["1"], "1": values["12"]}


@pytest.mark.parametrize("body", [
    "def bad(bool flag) -> uint[8] {\nif (flag) {\nreturn uint[8](1)\n}\n}\n",
    "def bad(bool flag) -> uint[8] {\nif (flag) {\nreturn uint[8](1)\n}\nreturn uint[16](2)\n}\n",
    "def bad(bool flag) -> uint[8] {\nif (flag) {\nreturn uint[8](1), flag\n}\nreturn uint[8](2)\n}\n",
])
def test_typed_helpers_reject_missing_or_incompatible_returns(runtime_target, body):
    with pytest.raises(QStackError, match="(?i)return|arity|type|width|path"):
        runtime_target(body + "qubit q[1]\nbit c[1]\nc[0]=measure q[0]\nuint[8] n=bad(c[0])\noutput n\n", "qasm3")


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
def test_broken_loop_does_not_reexecute_effectful_helper_predicate(runtime_target, fmt):
    artifact = runtime_target("""def probe(qubit a) -> (qubit, bool) {
ry(0.37) a
return a, true
}
qubit q[1]
while (probe(q[0])) max_iterations 2 {
break
}
""", fmt)
    _, actual = execute(artifact.physical_ir, max_qubits=1, max_paths=16)
    c, s = np.cos(.37 / 2), np.sin(.37 / 2)
    expected = np.array([[c, -s], [s, c]], dtype=complex).reshape(-1)
    expected = np.outer(expected, expected.conj())
    assert len(actual) == 1
    for (_, outputs), observed in actual.items():
        assert outputs and all(output[-1] == 0 for output in outputs)
        np.testing.assert_allclose(observed, expected, atol=2e-10, rtol=0)


def test_function_inlining_cannot_bypass_the_recorded_expansion_budget(runtime_target):
    source = ("def step(uint[8] n) -> uint[8] {\n" + "n = n + 1\n" * 10 +
              "return n\n}\nqubit q[1]\nuint[8] n = 0\n" +
              "n = step(n)\n" * 10 + "output n\n")
    with pytest.raises(QStackError, match="(?i)budget"):
        runtime_target(source, "qasm3", expanded_operation_budget=120)
    compiled = runtime_target(source, "qasm3", expanded_operation_budget=10000)
    assert compiled.manifest["limits"]["expanded_operation_budget"] == 10000
    result = submit_artifact(compiled, SubmissionOptions(mode="local", shots=2), wait=True)
    assert result.metadata["classical_counts"]["n"] == {"100": 2}
