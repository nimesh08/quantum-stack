"""Owned quantum compilation and explicit provider execution."""
from .models import CompiledArtifact, ExecutionResult, JobReceipt, QStackError, SubmissionOptions

__version__ = "0.6.0"


def run_source(source: str, *, language: str = "phonon", target: str,
               mode: str, shots: int = 1024, **options) -> ExecutionResult:
    from .service import run_source as execute
    return execute(source, language=language, target=target, mode=mode, shots=shots, **options)
