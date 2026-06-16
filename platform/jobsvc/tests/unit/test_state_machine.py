"""M1 — state machine unit tests.

Drive a Job through every legal transition; verify illegal ones raise.
"""

from __future__ import annotations

import uuid

import pytest

from jobsvc.models import (
    IllegalTransitionError,
    Job,
    JobState,
    LEGAL_TRANSITIONS,
    SourceKind,
    TERMINAL_STATES,
    User,
    UserRole,
)


def _make_job() -> Job:
    return Job(
        id=uuid.uuid4(),
        user_id=uuid.uuid4(),
        target="ibm_heron_r2",
        shots=1000,
        source="target generic\nqubit q[2]\nh q[0]\ncx q[0], q[1]\n",
        source_kind=SourceKind.spinor,
    )


def test_initial_state_is_submitted() -> None:
    assert _make_job().state == JobState.Submitted


def test_legal_path_completed() -> None:
    j = _make_job()
    j.transition(JobState.Queued)
    assert j.state == JobState.Queued
    assert j.queued_at is not None

    j.transition(JobState.Running)
    assert j.state == JobState.Running
    assert j.started_at is not None

    j.transition(JobState.Completed)
    assert j.state == JobState.Completed
    assert j.finished_at is not None
    assert j.is_terminal


def test_legal_path_rejected_up_front() -> None:
    j = _make_job()
    j.transition(JobState.Rejected, reason="over budget")
    assert j.state == JobState.Rejected
    assert j.rejection_reason == "over budget"
    assert j.finished_at is not None


def test_legal_path_failed_with_kind() -> None:
    j = _make_job()
    j.transition(JobState.Queued)
    j.transition(JobState.Running)
    j.transition(JobState.Failed, reason="timeout", error_kind="provider")
    assert j.state == JobState.Failed
    assert j.error_kind == "provider"
    assert j.rejection_reason == "timeout"


def test_running_back_to_queued_is_legal() -> None:
    """Lease expiry path: a worker crashes after claim; the next
    `claim()` re-queues the row."""
    j = _make_job()
    j.transition(JobState.Queued)
    j.transition(JobState.Running)
    j.transition(JobState.Queued)
    assert j.state == JobState.Queued


def test_queued_to_rejected_for_cancellation() -> None:
    j = _make_job()
    j.transition(JobState.Queued)
    j.transition(JobState.Rejected, reason="cancelled by user")
    assert j.is_terminal


@pytest.mark.parametrize(
    "from_state,to_state",
    [
        (JobState.Submitted, JobState.Running),
        (JobState.Submitted, JobState.Completed),
        (JobState.Submitted, JobState.Failed),
        (JobState.Queued, JobState.Completed),
        (JobState.Queued, JobState.Failed),
        (JobState.Running, JobState.Submitted),
        (JobState.Completed, JobState.Running),
        (JobState.Completed, JobState.Queued),
        (JobState.Failed, JobState.Running),
        (JobState.Rejected, JobState.Queued),
    ],
)
def test_illegal_transitions_raise(
    from_state: JobState, to_state: JobState
) -> None:
    j = _make_job()
    j.state = from_state
    with pytest.raises(IllegalTransitionError):
        j.transition(to_state)


def test_terminal_set_is_complete() -> None:
    assert TERMINAL_STATES == {
        JobState.Completed,
        JobState.Rejected,
        JobState.Failed,
    }


def test_legal_transitions_are_unique() -> None:
    assert len(LEGAL_TRANSITIONS) == len(set(LEGAL_TRANSITIONS))


def test_user_default_role() -> None:
    u = User(email="a@b.c", password_hash="x")
    # default applied at flush time; in-memory it's None unless set
    assert u.role in (UserRole.user, None)
