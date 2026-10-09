"""Feasible heterogeneous devices rejected by the former common-locus policy."""
import pytest
from qstack.models import QStackError, SubmissionOptions
from qstack.registry import cache_targets
from qstack.service import compile_file, find_binary, submit_artifact


@pytest.fixture
def compile_target(tmp_path, monkeypatch):
    try:
        find_binary("phononc")
    except QStackError:
        pytest.skip("C++ compiler required")
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path / "state"))
    def compile(record, text, level=0, **options):
        record = {"route": "ibm", "vendor": "ibm", "device": "heterogeneous", "qubits": 2,
            "native_gates": ["x", "y", "swap"], "all_to_all": False, "coupling": [[0, 1]],
            "formats": ["qiskit-native"], "capability_verified": True,
            "supports": {"feedforward": True, "mid_circuit_measure": True, "reset": False}, **record}
        cache_targets("ibm", [record])
        source = tmp_path / "program.phn"; source.write_text(text)
        return compile_file(source, target="heterogeneous", optimization_level=level,
                            config={"provider": "ibm", **options})
    return compile


@pytest.mark.parametrize("level", range(4))
def test_disjoint_per_wire_domains_compile_without_global_intersection(compile_target, level):
    artifact = compile_target({"gate_loci": {"x": [[0]], "y": [[1]], "measure": [[0], [1]]}},
        "target generic\nqubit q[2]\nbit c[2]\nx q[0]\ny q[1]\nc = measure q\n", level)
    assert artifact.physical_ir["initial_logical_to_physical"] == [0, 1]
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=16), wait=True)
    assert result.counts == {"11": 16}


@pytest.mark.parametrize("level", range(4))
def test_readout_requires_a_legal_routing_swap_and_preserves_sparse_destination(compile_target, level):
    artifact = compile_target({"gate_loci": {"x": [[0]], "measure": [[1]], "swap": [[0, 1]]}},
        "target generic\nqubit q[1]\nbit c[4]\nx q[0]\nc[3] = measure q[0]\n", level)
    assert artifact.physical_ir["initial_logical_to_physical"] == [0]
    assert artifact.physical_ir["logical_to_physical"] == [1]
    assert artifact.physical_ir["measurement_mapping"] == [{"qubit": 1, "clbit": 3}]
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=16), wait=True)
    assert result.counts == {"1000": 16}


def test_search_budget_and_swap_budget_are_actionable_distinct_failures(compile_target):
    record = {"gate_loci": {"x": [[0]], "measure": [[1]], "swap": [[0, 1]]}}
    source = "target generic\nqubit q[1]\nbit c[1]\nx q[0]\nc = measure q\n"
    with pytest.raises(QStackError, match="PLACEMENT_SEARCH_LIMIT"):
        compile_target(record, source, placement_max_states=1)
    with pytest.raises(QStackError, match="PLACEMENT_SWAP_LIMIT"):
        compile_target(record, source, placement_max_swaps=0)


def test_native_swap_uses_legacy_connectivity_when_no_explicit_locus_exists(compile_target):
    artifact = compile_target({"native_gates": ["x", "swap"],
        "gate_loci": {"x": [[0]], "measure": [[1]]}},
        "target generic\nqubit q[1]\nbit c[1]\nx q[0]\nc = measure q\n")
    assert artifact.physical_ir["logical_to_physical"] == [1]
    assert submit_artifact(artifact, SubmissionOptions(mode="local", shots=4), wait=True).counts == {"1": 4}


@pytest.mark.parametrize("level", range(4))
def test_distinct_native_entangler_loci_are_not_treated_as_one_basis(compile_target, level):
    artifact = compile_target({"qubits": 3, "native_gates": ["x", "sx", "rz", "cx", "cz"],
        "coupling": [[0, 1], [1, 2]], "directed_connectivity": True,
        "gate_loci": {"cz": [[0, 1]], "cx": [[1, 2]], "sx": [[0], [1], [2]],
                      "rz": [[0], [1], [2]], "x": [[0], [1], [2]], "measure": [[0], [1], [2]]}},
        "target generic\nqubit q[3]\nbit c[3]\nh q[0]\ncx q[0], q[1]\ncx q[1], q[2]\nc = measure q\n", level)
    for inst in artifact.physical_ir["instructions"]:
        if inst["op"] == "cz": assert inst["qubits"] == [0, 1]
        if inst["op"] == "cx": assert inst["qubits"] == [1, 2]
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=64), wait=True)
    assert set(result.counts) == {"000", "111"}


def test_forced_search_retains_legal_incumbent_when_budget_is_exhausted(compile_target):
    artifact = compile_target({}, "target generic\nqubit q[1]\nbit c[1]\nx q[0]\nc = measure q\n",
        placement_strategy="heterogeneous", placement_max_states=1)
    assert submit_artifact(artifact, SubmissionOptions(mode="local", shots=4), wait=True).counts == {"1": 4}


@pytest.mark.parametrize("level", range(4))
def test_common_branch_layout_preserves_entangled_instrument_and_readout(compile_target, level):
    from qstack.verification.engine import compare
    artifact = compile_target({"qubits": 3, "native_gates": ["x", "sx", "rz", "cz"],
        "coupling": [[0, 1], [1, 2]]},
        "target generic\nqubit q[3]\nbit c[3]\nh q[0]\ncx q[0], q[1]\nc[2] = measure q[1]\n"
        "if (c[2] == 1) {\ncx q[0], q[1]\ncx q[1], q[2]\ncx q[0], q[2]\n} else {\n"
        "cx q[0], q[1]\ncx q[1], q[2]\ncx q[0], q[2]\n}\nc[0] = measure q[0]\n", level)
    checked = compare(artifact.logical_ir, artifact.physical_ir, max_qubits=3, max_paths=64, threshold=1e-10)
    assert checked["status"] == "passed"


@pytest.mark.parametrize("level", range(4))
def test_distinct_single_qubit_synthesis_bases_preserve_complete_phase_operator(compile_target, level):
    from qstack.verification.engine import compare
    artifact = compile_target({"native_gates": ["u1q", "rz", "sx", "cz"],
        "gate_loci": {"u1q": [[0]], "rz": [[1]], "sx": [[1]], "cz": [[0, 1]]},
        "directed_connectivity": True},
        "target generic\nqubit q[2]\nrz(0.71) q[0]\nrx(0.41) q[1]\ncx q[0], q[1]\n"
        "ry(-0.83) q[0]\ncz q[0], q[1]\nrz(-0.37) q[1]\n", level)
    checked = compare(artifact.logical_ir, artifact.physical_ir, max_qubits=2, max_paths=4, threshold=1e-10)
    assert checked["status"] == "passed", checked


@pytest.mark.parametrize("strategy", ["uniform", "heterogeneous"])
@pytest.mark.parametrize("level", [0, 3])
def test_nested_unequal_branch_paths_preserve_complete_instrument(compile_target, strategy, level):
    from qstack.verification.engine import compare
    artifact = compile_target({"qubits": 4, "native_gates": ["u1q", "rz", "rxx"],
        "coupling": [[0, 1], [1, 2], [2, 3]]},
        "target generic\nqubit q[4]\nbit c[2]\nh q[0]\ncx q[0], q[1]\nh q[2]\nc[1] = measure q[2]\nc[0] = measure q[3]\n"
        "if (c[1] == 1) {\ncx q[0], q[3]\ncx q[1], q[2]\n"
        "if (c[0] == 1) {\ncx q[0], q[2]\n} else {\nry(0.37) q[3]\n}\n"
        "} else {\ncx q[0], q[3]\ncx q[1], q[3]\nrz(-0.29) q[0]\n}\n"
        "cx q[0], q[2]\nc[0] = measure q[3]\n", level, placement_strategy=strategy)
    checked = compare(artifact.logical_ir, artifact.physical_ir, max_qubits=4, max_paths=64, threshold=2e-9)
    assert checked["status"] == "passed", checked


@pytest.mark.parametrize("level", range(4))
def test_heterogeneous_negative_native_rotations_are_canonicalized_with_phase(compile_target, level):
    from qstack.verification.engine import compare
    artifact = compile_target({"native_gates": ["u1q", "rz", "rxx"],
        "gate_loci": {"u1q": [[0], [1]], "rz": [[0], [1]], "rxx": [[0, 1]]}},
        "target generic\nqubit q[2]\nu1q(-0.7, -0.9) q[0]\nrxx(-0.63) q[0], q[1]\n"
        "rz(-7.1) q[1]\n", level, placement_strategy="heterogeneous")
    checked = compare(artifact.logical_ir, artifact.physical_ir, max_qubits=2, max_paths=4, threshold=1e-10)
    assert checked["status"] == "passed", checked


def test_unequal_uniform_exits_keep_cheaper_shared_prefix_with_swap_cap(compile_target):
    from qstack.verification.engine import compare
    # Even pairs have identity action while making the O0 interaction placement
    # deterministic. The two branch exits differ; both can keep SWAP(0,1).
    source = ("target generic\nqubit q[4]\nbit c[4]\n" + "cx q[0], q[1]\n" * 10 +
              "cx q[1], q[2]\n" * 6 + "c[0] = measure q[1]\n"
              "if (c[0] == 1) {\ncx q[0], q[2]\ncx q[0], q[3]\n} else {\n"
              "cx q[0], q[2]\ncx q[3], q[0]\n}\nc[3] = measure q[0]\nc[1] = measure q[3]\n")
    record = {"qubits": 4, "native_gates": ["u1q", "cx", "swap"],
              "coupling": [[0, 1], [1, 2], [2, 3]]}
    artifact = compile_target(record, source, placement_strategy="uniform", placement_max_swaps=6)
    assert artifact.physical_ir["initial_logical_to_physical"] == [0, 1, 2, 3]
    assert artifact.physical_ir["logical_to_physical"] == [1, 0, 2, 3]
    assert sum(i["op"] == "swap" for i in artifact.physical_ir["instructions"]) == 6
    assert artifact.numerical_report["trial_statistics"]["branch_join_swaps_removed"] == 2
    checked = compare(artifact.logical_ir, artifact.physical_ir, max_qubits=4, max_paths=64, threshold=1e-10)
    assert checked["status"] == "passed", checked
    with pytest.raises(QStackError, match="PLACEMENT_SWAP_LIMIT"):
        compile_target(record, source, placement_strategy="uniform", placement_max_swaps=5)


def test_unequal_heterogeneous_exits_select_shared_nonentry_prefix(compile_target):
    from qstack.verification.engine import compare
    # The single-qubit loci have no common intersection, so this must exercise
    # the native heterogeneous router, not the retained uniform incumbent.
    artifact = compile_target({"qubits": 4, "native_gates": ["x", "y", "z", "cx", "swap"],
        "gate_loci": {"x": [[0]], "y": [[1]], "z": [[2]]},
        "coupling": [[0, 1], [1, 2], [2, 3]]},
        "target generic\nqubit q[4]\nbit c[4]\nx q[0]\ny q[1]\nz q[2]\nc[0] = measure q[1]\n"
        "if (c[0] == 1) {\ncx q[0], q[2]\ncx q[0], q[3]\n} else {\n"
        "cx q[0], q[2]\ncx q[3], q[0]\n}\nc[3] = measure q[0]\nc[1] = measure q[3]\n",
        placement_strategy="heterogeneous", placement_max_swaps=8)
    assert artifact.numerical_report["notes"]["placement_strategy"] == "heterogeneous"
    assert artifact.physical_ir["initial_logical_to_physical"] == [0, 1, 2, 3]
    assert artifact.physical_ir["logical_to_physical"] != [0, 1, 2, 3]
    assert sum(i["op"] == "swap" for i in artifact.physical_ir["instructions"]) == 8
    assert artifact.numerical_report["trial_statistics"]["branch_join_swaps_removed"] == 6
    checked = compare(artifact.logical_ir, artifact.physical_ir, max_qubits=4, max_paths=64, threshold=1e-10)
    assert checked["status"] == "passed", checked
