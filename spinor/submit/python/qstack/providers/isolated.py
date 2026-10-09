"""SDK credential-file overrides scoped to a child process, never os.environ.

QCS and Nexus currently discover their custom configuration paths at SDK import
time using environment variables. Each call has an isolated SDK process. Secrets
travel through stdin, not command arguments, and provider console output is not
returned as a credential-bearing error message. Submissions are never retried.
"""
from __future__ import annotations

import base64
from dataclasses import asdict
import json
import os
import subprocess
import sys

from qstack.models import CompiledArtifact, ExecutionResult, JobReceipt, QStackError


ENV_FIELDS = {"rigetti": {"sdk_config_file": "QCS_SETTINGS_FILE_PATH", "tokens_file": "QCS_SECRETS_FILE_PATH"},
              "quantinuum": {"sdk_config_file": "NEXUS_CONFIG_FILE"}}


class IsolatedSDKAdapter:
    def __init__(self, adapter):
        self.route, self.config, self.capabilities = adapter.route, adapter.config, adapter.capabilities

    def _call(self, method, **arguments):
        config = dict(self.config)
        config["_isolated_sdk_worker"] = True
        environment = dict(os.environ)
        for field, name in ENV_FIELDS[self.route].items():
            if config.get(field):
                environment[name] = str(config[field])
        request = {"route": self.route, "config": config, "method": method, "arguments": arguments}
        try:
            response = subprocess.run([sys.executable, "-m", "qstack.providers.isolated"],
                input=json.dumps(request), text=True, capture_output=True, env=environment,
                timeout=float(config.get("timeout", 600)), check=False)
        except (OSError, subprocess.TimeoutExpired):
            raise QStackError("Provider SDK process failed or timed out; a submission may have created a job. Do not resubmit automatically.",
                              "SDK_PROCESS_FAILED") from None
        try:
            result = json.loads(response.stdout)
        except (ValueError, TypeError):
            raise QStackError("Provider SDK process returned no valid receipt; do not resubmit automatically", "SDK_PROCESS_FAILED") from None
        if response.returncode != 0 or "error" in result:
            raise QStackError(result.get("error", "Provider SDK process failed"), result.get("code", "PROVIDER_ERROR"))
        value = result["value"]
        return JobReceipt.from_dict(value) if method == "submit" else ExecutionResult.from_dict(value) if method == "results" else value

    def check_auth(self):
        return self._call("check_auth")

    def discover(self):
        return self._call("discover")

    def login(self):
        return self._call("login")

    def submit(self, artifact, options):
        data = asdict(artifact)
        data["payload"] = base64.b64encode(artifact.program_bytes()).decode("ascii")
        return self._call("submit", artifact=data, options=asdict(options))

    def status(self, receipt):
        return self._call("status", receipt=receipt.to_dict())

    def results(self, receipt):
        return self._call("results", receipt=receipt.to_dict())

    def cancel(self, receipt):
        return self._call("cancel", receipt=receipt.to_dict())


def main():
    from contextlib import redirect_stdout, redirect_stderr
    from io import StringIO
    from qstack.models import SubmissionOptions
    from qstack.providers import get_adapter

    try:
        request = json.load(sys.stdin)
        method = request["method"]
        if method not in {"check_auth", "discover", "login", "submit", "status", "results", "cancel"}:
            raise QStackError("Unsupported provider SDK operation")
        kwargs = request["arguments"]
        if method == "submit":
            kwargs["artifact"]["payload"] = base64.b64decode(kwargs["artifact"]["payload"], validate=True)
            kwargs["artifact"] = CompiledArtifact(**kwargs["artifact"])
            kwargs["options"] = SubmissionOptions(**kwargs["options"])
        elif "receipt" in kwargs:
            kwargs["receipt"] = JobReceipt.from_dict(kwargs["receipt"])
        with redirect_stdout(StringIO()), redirect_stderr(StringIO()):
            value = getattr(get_adapter(request["route"], request["config"]), method)(**kwargs)
        print(json.dumps({"value": value.to_dict() if hasattr(value, "to_dict") else value}))
    except QStackError as exc:
        print(json.dumps({"error": str(exc), "code": exc.code}))
        return 1
    except Exception:
        print(json.dumps({"error": "Provider SDK call failed; inspect account configuration and provider dashboard before any resubmission",
                          "code": "PROVIDER_ERROR"}))
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
