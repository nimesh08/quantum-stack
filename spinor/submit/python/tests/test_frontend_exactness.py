"""Public compiler regressions for exact static values and expansion bounds."""
import pytest

from qstack.models import QStackError, SubmissionOptions
from qstack.service import submit_artifact
from test_runtime_classical import runtime_target


def test_last_statement_cannot_exceed_the_expansion_budget(runtime_target):
    with pytest.raises(QStackError, match="(?i)budget"):
        runtime_target("qubit q[3]\n", "qasm3", expanded_operation_budget=2)


@pytest.mark.parametrize("fmt", ["qasm3", "qir-text"])
def test_static_helper_and_index_arithmetic_are_exact_above_binary64(runtime_target, fmt):
    artifact = runtime_target("""qubit q[2]
bit c[2]
int base=9007199254740992
def f(qubit a,int value) {
if (value == 9007199254740993) {
x a
}
}
f(q[0],base+1)
x q[(base+1)-base]
for index in base..base+2 {
z q[index-base]
}
int next=base
while (next < base+2) {
z q[next-base]
next=next+1
}
c[0]=measure q[0]
c[1]=measure q[1]
""", fmt)
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=4), wait=True)
    assert result.counts == {"11": 4}


def test_repeated_controller_helper_binds_each_measurement_argument(runtime_target):
    artifact = runtime_target("""def conditioned(qubit a,bit flag) {
if (flag == 1) {
x a
}
}
qubit q[3]
bit c[3]
x q[0]
c[0]=measure q[0]
c[1]=measure q[1]
uint[8] controller=0
conditioned(q[2],c[0])
conditioned(q[2],c[1])
c[2]=measure q[2]
bool result=c[2]
output result
""", "qasm3")
    result = submit_artifact(artifact, SubmissionOptions(mode="local", shots=8), wait=True)
    assert result.metadata["classical_counts"]["result"] == {"1": 8}
