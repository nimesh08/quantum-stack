"""Manual validation of an existing artifact; offline unless explicitly selected.

Run ``python -m qstack.validation_harness ARTIFACT --level account|hardware``
only when authenticated reads or live submission are intended. Creation is
never retried. All provider actions use the same service as the public CLI.
"""
from __future__ import annotations
import argparse
from dataclasses import asdict
from datetime import datetime, timezone
import importlib.metadata
import json
import math
from pathlib import Path

from .artifacts import artifact_hash, load_artifact
from .config import redact_data, resolve_config
from .jobs import read_with_retry, save_job, wait_for_result
from .models import QStackError, SubmissionOptions
from .providers import get_adapter
from .registry import digest
from .service import submit_artifact
from .verification import verify_artifact


def _sampling_check(counts, accepted, minimum):
    if not accepted:
        return {"status": "not_requested"}
    if not isinstance(counts, dict) or not counts or any(type(v) is not int or v < 0 for v in counts.values()):
        return {"status": "not_checked", "reason": "Provider did not supply an integer shot histogram"}
    shots = sum(counts.values())
    if not shots:
        return {"status": "not_checked", "reason": "No observed shots"}
    successes = sum(value for key, value in counts.items() if key in accepted)
    fraction, z = successes / shots, 1.959963984540054
    denominator = 1 + z*z/shots
    center = (fraction + z*z/(2*shots))/denominator
    radius = z*math.sqrt(fraction*(1-fraction)/shots + z*z/(4*shots*shots))/denominator
    lower, upper = max(0, center-radius), min(1, center+radius)
    return {"status": "observed" if minimum is None else "passed" if lower >= minimum else "below_threshold",
        "shots": shots, "accepted_shots": successes, "observed_fraction": fraction,
        "wilson_95_percent": [lower, upper], "minimum_probability": minimum,
        "scope": "readout support check; not entanglement or calibrated-fidelity certification"}


def run_validation(artifact, *, level="offline", config=None, shots=128, cost_cap_usd=None,
                   expected_bitstrings=(), minimum_success_probability=None, output=None):
    if level not in {"offline", "account", "hardware"}:
        raise QStackError("Validation level must be offline, account or hardware")
    if minimum_success_probability is not None and (type(minimum_success_probability) not in {int, float}
            or not math.isfinite(minimum_success_probability) or not 0 <= minimum_success_probability <= 1):
        raise QStackError("Minimum success probability must be finite and in [0,1]")
    if minimum_success_probability is not None and not expected_bitstrings:
        raise QStackError("A sampling threshold requires explicit expected bitstrings")
    if any(not isinstance(bits, str) or not bits or set(bits) - {"0", "1"} for bits in expected_bitstrings):
        raise QStackError("Expected bitstrings must contain only zero and one")
    config = dict(config or {})
    if config.get("provider", artifact.route) != artifact.route:
        raise QStackError("Harness provider must match the artifact route")
    if config.get("device", artifact.target_snapshot.get("device")) != artifact.target_snapshot.get("device"):
        raise QStackError("Harness device must match the compiled artifact")
    options = SubmissionOptions(mode="live", shots=shots, cost_cap_usd=cost_cap_usd, name="qstack-validation")
    evidence = {"schema_version": 1, "level": level, "started_at": datetime.now(timezone.utc).isoformat(),
        "artifact_hash": artifact_hash(artifact), "route": artifact.route, "target": artifact.target,
        "device": artifact.target_snapshot.get("device"), "snapshot_hash": digest(artifact.target_snapshot),
        "compiler_version": artifact.manifest.get("compiler_version"), "source_hash": artifact.manifest.get("source_hash"),
        "readout_order": artifact.manifest.get("readout_order", "c[n-1]...c[0]"),
        "contract_tested": False, "account_access_verified": False, "live_execution_completed": False,
        "laboratory_configured": False,
        "contract_test_scope": "this artifact's offline target and serialization checks; not the provider adapter test suite",
        "hardware_execution_verified": False, "status": "running", "shots": shots,
        "cost_cap_usd": cost_cap_usd, "submission_attempts": 0, "sdk_versions": {}}
    for package in ("qiskit-ibm-runtime", "cirq-google", "amazon-braket-sdk", "qdk", "qnexus", "qcs-sdk-python",
                    "iqm-client", "oqc-qcaas-client", "qiskit-aqt-provider", "qibolab"):
        try:
            evidence["sdk_versions"][package] = importlib.metadata.version(package)
        except importlib.metadata.PackageNotFoundError:
            pass

    def save():
        cleaned = redact_data(evidence, config)
        if output is not None:
            destination = Path(output)
            destination.parent.mkdir(parents=True, exist_ok=True)
            temporary = destination.with_name(destination.name + ".tmp")
            temporary.write_text(json.dumps(cleaned, indent=2, allow_nan=False), encoding="utf-8")
            temporary.replace(destination)
        return cleaned

    try:
        evidence["serialization"] = submit_artifact(artifact, options, config, dry_run=True)
        evidence["contract_tested"] = True
        evidence["independent_verification"] = verify_artifact(artifact, store=False)
        if evidence["independent_verification"]["status"] == "failed":
            raise QStackError("Independent verification failed; no authenticated request or job was attempted", "VERIFICATION_FAILED")
        if level != "offline":
            adapter = get_adapter(artifact.route, config)
            authenticated = read_with_retry(adapter.check_auth)
            evidence["authentication"] = authenticated
            evidence["account_access_verified"] = authenticated.get("authenticated") is True
            evidence["laboratory_configured"] = (artifact.route == "qibolab"
                and authenticated.get("authenticated") is None and authenticated.get("configured") is True)
            if not evidence["account_access_verified"] and not evidence["laboratory_configured"]:
                raise QStackError("Provider account check did not verify access", "AUTHENTICATION_REQUIRED")
        if level == "hardware":
            # submit_artifact performs fresh capability and requested cost checks.
            # No callback or retry wrapper surrounds this side-effecting call.
            evidence["submission_attempts"] = 1
            receipt = submit_artifact(artifact, options, config, wait=False)
            evidence["receipt"] = asdict(receipt)
            evidence["receipt_path"] = save_job(receipt, config=config)
            save()  # job identity survives timeouts and subsequent process failure
            if adapter.capabilities.get("status", False):
                result = wait_for_result(adapter, receipt, timeout=config.get("timeout", 600),
                                         poll_interval=config.get("poll_interval", 2), config=config)
            else:
                result = read_with_retry(lambda: adapter.results(receipt))
                save_job(receipt, result, config)
            evidence["result"] = asdict(result)
            evidence["live_execution_completed"] = True
            # Classification comes from the fresh submission check, never from
            # an older artifact or from the user's requested validation level.
            evidence["execution_kind"] = receipt.metadata.get("device_execution_kind", "unknown")
            evidence["execution_kind_source"] = receipt.metadata.get("device_execution_kind_source")
            evidence["hardware_execution_verified"] = (evidence["execution_kind"] == "hardware"
                and receipt.metadata.get("device_execution_kind_verified") is True)
            evidence["sampling"] = _sampling_check(result.counts, set(expected_bitstrings), minimum_success_probability)
        evidence["status"] = "completed"
    except QStackError as error:
        evidence["status"] = "failed"
        evidence["error"] = {"code": error.code, "message": str(error)}
        evidence["do_not_resubmit_automatically"] = evidence["submission_attempts"] > 0
    except Exception as error:
        # SDK exceptions may contain URLs, token echoes or session payloads.
        # Preserve the exception class and action state, never its unsafe text.
        evidence["status"] = "failed"
        evidence["error"] = {"code": "PROVIDER_SDK_ERROR", "exception_type": type(error).__name__,
            "message": "Provider SDK operation failed; inspect the saved receipt and retrieve an existing job before considering another submission"}
        evidence["do_not_resubmit_automatically"] = evidence["submission_attempts"] > 0
    evidence["finished_at"] = datetime.now(timezone.utc).isoformat()
    return save()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("artifact")
    parser.add_argument("--level", choices=("offline", "account", "hardware"), default="offline")
    parser.add_argument("--shots", type=int, default=128)
    parser.add_argument("--cost-cap-usd", type=float)
    parser.add_argument("--expected-bitstring", action="append", default=[])
    parser.add_argument("--minimum-success-probability", type=float)
    parser.add_argument("--env-file", "--env-path", action="append", default=argparse.SUPPRESS)
    parser.add_argument("--no-env-file", action="store_true", default=argparse.SUPPRESS)
    for flag in ("config", "profile", "sdk-profile"):
        parser.add_argument("--" + flag, default=argparse.SUPPRESS)
    parser.add_argument("--output", required=True, help="Persist redacted validation evidence")
    args = vars(parser.parse_args())
    try:
        artifact = load_artifact(args["artifact"])
        resolved = resolve_config(artifact.route, args)
        evidence = run_validation(artifact, level=args["level"], config=resolved.values, shots=args["shots"],
            cost_cap_usd=args.get("cost_cap_usd"), expected_bitstrings=args["expected_bitstring"],
            minimum_success_probability=args.get("minimum_success_probability"), output=args["output"])
        print(json.dumps(evidence, indent=2, allow_nan=False))
        return 0 if evidence["status"] == "completed" and evidence.get("sampling", {}).get("status") not in {"below_threshold", "not_checked"} else 1
    except QStackError as error:
        print(json.dumps({"status": "failed", "error": {"code": error.code, "message": str(error)}}))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
