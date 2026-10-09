"""Source semantics checked against complete instruments and actual C++ shots."""
import pytest
from qstack.classical import CLASSICAL_OPS
from qstack.models import SubmissionOptions, QStackError
from qstack.registry import cache_targets
from qstack.service import compile_file, find_binary, submit_artifact
from qstack.verification import verify_artifact


@pytest.fixture
def runtime_target(tmp_path, monkeypatch):
    try: find_binary("phononc")
    except QStackError: pytest.skip("Compiler binaries required")
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path / "state"))
    snapshot = {"route": "quantinuum", "vendor": "quantinuum", "device": "offline-controller-fixture",
                "qubits": 3, "native_gates": ["u1q", "rz", "rzz"], "qir_platform": "quantinuum-h2",
                "all_to_all": True, "coupling": [], "formats": ["qasm3", "qir-text", "qir-bitcode"], "capability_verified": True,
                "supports": {"feedforward": True, "reset": True, "mid_circuit_measure": True},
                "capability_sources": ["Offline controller fixture; not account or hardware evidence"],
                "capabilities": {"features": {**{"classical."+op[2:]: "supported" for op in CLASSICAL_OPS},
                                             "branching.bit": "supported", "measure": "supported", "reset": "supported",
                                             "output.loop_exhausted": "supported", "output.classical": "supported"}, "integer_widths": list(range(1,65))}}
    cache_targets("quantinuum", [snapshot])
    def compile(source, fmt, level=2, target_overrides=None, **options):
        if target_overrides:
            cache_targets("quantinuum", [{**snapshot, **target_overrides}])
        path = tmp_path / "runtime.phn"
        path.write_text("target generic\n" + source)
        artifact = compile_file(path, target=snapshot["device"], config={"provider": "quantinuum", **options}, format=fmt,
                                optimization_level=level)
        evidence = verify_artifact(artifact, store=False)
        assert evidence["status"] == "passed", evidence["checks"]
        return artifact
    return compile


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
@pytest.mark.parametrize("initial", [0, 1])
def test_snapshot_branch_join_uint_wraparound(runtime_target, fmt, initial):
    source = "qubit q[2]\nbit c[2]\n" + ("x q[0]\n" if initial else "") + """
c[0] = measure q[0]
int saved = c[0]
uint[8] value = 250
reset q[0]
c[0] = measure q[0]
if (saved == 1) {
value = value + 10
x q[1]
} else {
value = value - 10
}
c[1] = measure q[1]
output value
output saved
"""
    artifact = runtime_target(source, fmt)
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=32), wait=True)
    assert result.metadata["classical_counts"]["saved"] == {str(initial): 32}
    assert result.metadata["classical_counts"]["value"] == {str(4 if initial else 240): 32}
    assert sum(result.counts.values()) == 32


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
def test_wide_integer_never_rounds_through_binary64(runtime_target, fmt):
    artifact = runtime_target("qubit q[1]\nuint[64] large = 9007199254740993\nuint[64] value = large + 2\noutput value\n", fmt)
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=2), wait=True)
    assert result.metadata["classical_counts"]["value"] == {"9007199254740995": 2}


@pytest.mark.parametrize("bits", range(4))
def test_boolean_truth_tables(runtime_target, bits):
    source = "qubit q[2]\nbit c[2]\n"
    source += "".join(f"x q[{i}]\n" for i in range(2) if bits & (1 << i))
    source += "c[0] = measure q[0]\nc[1] = measure q[1]\nbool a = c[0]\nbool b = c[1]\n"
    source += "bool both = a & b\nbool either = a | b\nbool parity = a ^ b\nbool opposite = !a\noutput both\noutput either\noutput parity\noutput opposite\n"
    artifact = runtime_target(source, "qasm3")
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=2), wait=True)
    a, b = bits & 1, (bits >> 1) & 1
    assert result.metadata["classical_counts"] == {k: {str(v): 2} for k,v in
        {"both": a & b, "either": a | b, "parity": a ^ b, "opposite": 1-a}.items()}


@pytest.mark.parametrize("width", [1, 2, 8, 32, 53, 64])
def test_unsigned_max_wraps_exactly(runtime_target, width):
    source = f"qubit q[1]\nuint[{width}] value = {(1 << width)-1}\nvalue = value + 1\noutput value\n"
    artifact = runtime_target(source, "qir-text")
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=2), wait=True)
    assert result.metadata["classical_counts"]["value"] == {"0": 2}


def test_direct_measurement_predicate_joins_runtime_values(runtime_target):
    artifact = runtime_target("""qubit q[1]
bit c[1]
h q[0]
c[0] = measure q[0]
uint[8] value = 3
if (c[0] == 1) {
value = value + 4
}
output value
""", "qasm3")
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=64), wait=True)
    assert set(result.metadata["classical_counts"]["value"]) == {"3", "7"}


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
@pytest.mark.parametrize("stop", range(5))
def test_bounded_loop_terminates_at_each_iteration(runtime_target, fmt, stop):
    artifact = runtime_target(f"""qubit q[1]
uint[8] count = 0
bounded while (count < {stop}) max_iterations 4 {{
x q[0]
count = count + 1
}}
output count
""", fmt)
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=8), wait=True)
    assert result.metadata["classical_counts"]["count"] == {str(stop): 8}
    assert result.metadata["application_status"] == "completed"
    assert result.metadata["exhausted_shots"] == 0
    assert sum(result.counts.values()) == 8


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
def test_mixed_loop_exhaustion_preserves_every_correlated_shot(runtime_target, fmt):
    from qstack.jobs import load_job
    artifact = runtime_target("""qubit q[1]
bit c[1]
h q[0]
c[0] = measure q[0]
bool run = c[0]
uint[8] count = 0
bounded while (run == 1) max_iterations 3 {
count = count + 1
}
output count
output run
""", fmt)
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=64), wait=True)
    assert result.metadata["application_status"] == "loop_exhausted"
    assert 0 < result.metadata["exhausted_shots"] < 64
    assert sum(result.counts.values()) == 64
    assert result.metadata["successful_shots"] + result.metadata["exhausted_shots"] == 64
    assert result.metadata["classical_counts"]["count"]["3"] == result.metadata["exhausted_shots"]
    assert result.metadata["classical_counts"]["run"]["1"] == result.metadata["exhausted_shots"]
    _, restored = load_job(result.job_id)
    assert restored.counts == result.counts
    assert restored.metadata == result.metadata


def test_bounded_loop_checks_predicate_after_last_allowed_body(runtime_target):
    artifact = runtime_target("""qubit q[1]
bit c[1]
x q[0]
c[0] = measure q[0]
uint[8] count = 0
bounded while (c[0] == 1) max_iterations 1 {
reset q[0]
c[0] = measure q[0]
count = count + 1
}
output count
""", "qir-text")
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=4), wait=True)
    assert result.metadata["classical_counts"]["count"] == {"1": 4}
    assert result.metadata["application_status"] == "completed"


def test_nested_bounded_loops_have_defined_flags_even_on_skipped_paths(runtime_target):
    artifact = runtime_target("""qubit q[1]
bit c[1]
h q[0]
c[0] = measure q[0]
uint[8] outer = 0
uint[8] total = 0
uint[8] limit = uint[8](c[0])
bounded while (outer < limit) max_iterations 2 {
uint[8] inner = 0
bounded while (inner < 2) max_iterations 2 {
inner = inner + 1
total = total + 1
}
outer = outer + 1
}
output total
""", "qasm3")
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=32), wait=True)
    assert set(result.metadata["classical_counts"]["total"]) == {"0", "2"}
    assert result.metadata["application_status"] == "completed"


def test_expansion_budget_is_enforced_and_recorded(runtime_target):
    with pytest.raises(QStackError, match="(?i)budget"):
        runtime_target("qubit q[1]\nuint[8] n=0\nbounded while (n<10) max_iterations 10 {\nn=n+1\n}\n", "qasm3", expanded_operation_budget=12)
    artifact = runtime_target("qubit q[1]\nx q[0]\n", "qasm3", expanded_operation_budget=321)
    assert artifact.manifest["limits"]["expanded_operation_budget"] == 321


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
def test_unsigned_bitwise_shifts_casts_and_comparisons(runtime_target, fmt):
    artifact = runtime_target("""qubit q[1]
uint[8] a = 173
uint[8] b = 90
uint[8] both = a & b
uint[8] either = a | b
uint[8] different = a ^ b
uint[8] left = a << 3
uint[8] right = a >> 3
uint[4] narrow = uint[4](a)
uint[64] wide = uint[64](a)
bool less = a < b
bool greater = a >= b
bool equal = a == b
bool unequal = a != b
output both
output either
output different
output left
output right
output narrow
output wide
output less
output greater
output equal
output unequal
""", fmt)
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=4), wait=True)
    expected = {"both": 173 & 90, "either": 173 | 90, "different": 173 ^ 90,
                "left": (173 << 3) & 255, "right": 173 >> 3, "narrow": 173 & 15,
                "wide": 173, "less": 0, "greater": 1, "equal": 0, "unequal": 1}
    assert result.metadata["classical_counts"] == {name: {str(value): 4} for name, value in expected.items()}


@pytest.mark.parametrize("body,diagnostic", [
    ("uint[8] a=2\nuint[8] b=3\nuint[8] out=a*b\n", "unsupported"),
    ("uint[8] a=8\nuint[8] b=2\nuint[8] out=a/b\n", "unsupported"),
    ("uint[8] a=8\nuint[8] shift=uint[8](c[0])\nuint[8] out=a<<shift\n", "shift"),
    ("uint[8] a=8\nuint[8] out=a<<8\n", "shift"),
    ("uint[8] a=8\nuint[16] b=2\nuint[8] out=a+b\n", "width"),
    ("uint[8] index=uint[8](c[0])\nx q[index]\n", "compile-time"),
    ("uint[8] angle=uint[8](c[0])\nrx(angle) q[0]\n", "angle"),
    ("if (c[0] == 1) {\nuint[8] inside=7\n}\noutput inside\n", "unknown"),
])
def test_unsupported_runtime_operations_fail_with_diagnostics(runtime_target, body, diagnostic):
    with pytest.raises(QStackError, match=f"(?i){diagnostic}"):
        runtime_target("qubit q[1]\nbit c[1]\nc[0]=measure q[0]\n" + body, "qasm3")


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
@pytest.mark.parametrize("initial", [0, 1])
def test_conditional_quantum_allocation_discard_and_reuse(runtime_target, fmt, initial):
    artifact = runtime_target("qubit q[1]\nbit c[2]\n" + ("x q[0]\n" if initial else "") + """
c[0] = measure q[0]
if (c[0] == 1) {
qubit ancilla[1]
h ancilla[0]
cx ancilla[0],q[0]
discard ancilla
}
qubit fresh[1]
c[1] = measure fresh[0]
bool zero = c[1]
output zero
""", fmt)
    assert artifact.physical_ir["reserved_pool"]
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=16), wait=True)
    assert result.metadata["classical_counts"]["zero"] == {"0": 16}
    assert sum(result.counts.values()) == 16


def test_quantum_pool_checks_capacity_scope_and_reset_capability(runtime_target):
    with pytest.raises(QStackError, match="(?i)capacity|qubits|qubit count"):
        runtime_target("qubit q[2]\nbit c[1]\nc[0]=measure q[0]\nif (c[0] == 1) {\nqubit ancilla[2]\nh ancilla[0]\n}\n", "qasm3")
    with pytest.raises(QStackError, match="(?i)unknown|scope"):
        runtime_target("qubit q[1]\nbit c[1]\nc[0]=measure q[0]\nif (c[0] == 1) {\nqubit ancilla[1]\n}\nx ancilla[0]\n", "qasm3")
    with pytest.raises(QStackError, match="(?i)reset"):
        runtime_target("qubit q[1]\nx q[0]\ndiscard q\nqubit fresh[1]\n", "qasm3",
                       target_overrides={"supports": {"feedforward": True, "reset": False, "mid_circuit_measure": True},
                                         "capabilities": {"features": {"reset": "unsupported"}}})


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
def test_loop_break_continue_and_complement_instruments(runtime_target, fmt):
    artifact = runtime_target("""qubit q[1]
uint[8] n = 0
uint[8] total = 0
while (n < 8) max_iterations 8 {
n = n + 1
if (n == 2) {
continue
}
if (n == 4) {
break
}
total = total + 1
}
uint[8] complement = ~n
output n
output total
output complement
""", fmt)
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=4), wait=True)
    assert result.metadata["classical_counts"]["n"] == {"4": 4}
    assert result.metadata["classical_counts"]["total"] == {"2": 4}
    assert result.metadata["classical_counts"]["complement"] == {"251": 4}
    assert result.metadata["application_status"] == "completed"
