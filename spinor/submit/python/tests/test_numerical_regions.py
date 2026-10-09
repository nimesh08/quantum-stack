"""Accepted local observations survive dynamic fences without a global bound."""
import json
import math

import pytest

from qstack.artifacts import load_artifact
from qstack.classical import CLASSICAL_OPS
from qstack.models import QStackError
from qstack.registry import cache_targets
from qstack.service import compile_file, find_binary


@pytest.mark.parametrize("level", range(4))
def test_dynamic_region_sums_match_only_the_accepted_trace(tmp_path, monkeypatch, level):
    try:
        find_binary("phononc")
        find_binary("spinorc")
    except QStackError:
        pytest.skip("Compiler binaries required")
    monkeypatch.setenv("QSTACK_STATE_DIR", str(tmp_path / "state"))
    target = {"route": "quantinuum", "vendor": "quantinuum", "device": "numerical-regions-fixture",
              "qubits": 2, "native_gates": ["u1q", "rz", "rzz"], "all_to_all": True,
              "coupling": [], "formats": ["qasm3"], "capability_verified": True,
              "supports": {"feedforward": True, "reset": True, "mid_circuit_measure": True},
              "capability_sources": ["Offline numerical-report fixture; no account or hardware evidence"],
              "capabilities": {"features": {**{"classical."+op[2:]: "supported" for op in CLASSICAL_OPS},
                  "branching.bit": "supported", "measure": "supported", "reset": "supported",
                  "output.classical": "supported"}, "integer_widths": [8]}}
    cache_targets("quantinuum", [target])
    source = tmp_path / "regions.phn"
    source.write_text("""target generic
qubit q[2]
bit c[1]
h q[0]
rx(4.1) q[0]
c[0] = measure q[1]
h q[0]
rx(4.1) q[0]
if (c[0] == 1) {
  h q[0]
  rx(4.1) q[0]
} else {
  h q[0]
  rx(4.1) q[0]
}
h q[0]
rx(4.1) q[0]
reset q[0]
h q[0]
rx(4.1) q[0]
uint[8] value = 1
h q[0]
rx(4.1) q[0]
output value
""", encoding="utf-8")
    output = tmp_path / "compiled"
    artifact = compile_file(source, target=target["device"], config={"provider": "quantinuum"},
                            output=output, format="qasm3", optimization_level=level)
    report = json.loads((output / "numerical.json").read_text(encoding="utf-8"))
    assert report == artifact.numerical_report == load_artifact(output).numerical_report
    assert report["coverage"]["has_nonunitary_operations"] is True
    assert report["whole_program_error"] is None
    assert report["certified"] is False
    assert report["approximation_error_budget"] == 0
    assert report["coverage"]["complete"] is False

    # The source contains one non-native H in each deliberately separated
    # region. Derive its expected fence ordinal independently from the saved
    # logical instruction order; synthesized gate counts do not enter it.
    boundary_names = {"measure", "reset", "barrier", "if", "else", "endif"}
    expected = set()
    region = 0
    for instruction in artifact.logical_ir["instructions"]:
        name = instruction["op"]
        if name == "h":
            expected.add(f"unitary-{region}")
        if name in boundary_names or name.startswith("c_"):
            region += 1
    assert len(expected) == 7
    observed = {event["region"] for event in report["rewrites"] if event["pass"] == "native.decomposition"}
    assert expected <= observed

    assert len(report["regions"]) >= len(expected)
    for aggregate in report["regions"]:
        events = [event for event in report["rewrites"] if event["region"] == aggregate["id"]]
        measured = [event["observed_frobenius_residual"] for event in events
                    if event["observed_frobenius_residual"] is not None]
        assert aggregate["accepted_rewrites"] == len(events)
        assert aggregate["checked_rewrites"] == len(measured)
        assert aggregate["unmeasured_rewrites"] == len(events) - len(measured)
        assert aggregate["aggregation_available"] is bool(measured)
        if measured:
            assert aggregate["sum_observed_local_residuals"] == pytest.approx(math.fsum(measured), rel=1e-14, abs=0)
            assert aggregate["max_observed_local_residual"] == max(measured)
        else:
            assert aggregate["sum_observed_local_residuals"] is None
            assert aggregate["max_observed_local_residual"] is None
