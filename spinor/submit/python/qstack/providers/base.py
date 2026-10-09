"""Transport primitives shared by the optional provider adapters.

No provider is imported, contacted, or logged into when this package is imported.
Writes are never retried automatically: a timed-out submit may have created a job.
"""
from __future__ import annotations

import hashlib
import importlib
import json
import sys
import uuid
from enum import Enum
from urllib.error import HTTPError, URLError
from urllib.parse import quote, urlsplit
from urllib.request import HTTPRedirectHandler, Request, build_opener

from qstack.models import CompiledArtifact, ExecutionResult, JobReceipt, QStackError


def optional(module: str, extra: str):
    if extra == "oqc" and sys.version_info[:2] != (3, 12):
        raise QStackError("The pinned OQC SDK requires Python 3.12; use a Python 3.12 environment and install the oqc extra", "MISSING_DEPENDENCY")
    try:
        return importlib.import_module(module)
    except ImportError as exc:
        raise QStackError(f"Install the qstack '{extra}' optional dependency to use this route", "MISSING_DEPENDENCY") from exc


def plain(value):
    """Convert result values, never client objects or credential containers."""
    if isinstance(value, Enum):
        return value.value
    if value is None or isinstance(value, (str, int, float, bool)):
        return value
    if isinstance(value, complex):
        return {"real": value.real, "imag": value.imag}
    if isinstance(value, dict):
        return {str(k): plain(v) for k, v in value.items()}
    if isinstance(value, (tuple, list)):
        return [plain(v) for v in value]
    if hasattr(value, "model_dump"):
        return plain(value.model_dump(mode="json"))
    if hasattr(value, "to_dict"):
        return plain(value.to_dict())
    if hasattr(value, "tolist"):
        return plain(value.tolist())
    if isinstance(value, uuid.UUID):
        return str(value)
    # SDK result containers vary; callers select public fields explicitly.
    return str(value)


def segment(value):
    return quote(str(value), safe="")


def counts_dict(value):
    """Only actual integer histograms qualify; probabilities remain raw results."""
    if isinstance(value, dict) and value and all(isinstance(v, int) and not isinstance(v, bool) and v >= 0 for v in value.values()):
        return {str(k): v for k, v in value.items()}
    return None


class Adapter:
    route = ""
    capabilities = {"submit": True, "status": True, "results": True, "cancel": True,
                    "discover": True, "login": False, "vendor_transpilation": False}

    def __init__(self, config=None):
        self.config = dict(config or {})

    def need(self, key, *aliases):
        for name in (key, *aliases):
            if self.config.get(name) is not None and self.config[name] != "":
                return self.config[name]
        raise QStackError(f"{self.route} requires configuration '{key}'", "MISSING_CREDENTIALS")

    def validate(self, artifact, options, formats=None):
        if artifact.route != self.route:
            raise QStackError(f"Artifact route {artifact.route} does not match {self.route}")
        if options.mode != "live":
            raise QStackError("Provider adapters only execute live mode; use the local/cassette executor")
        if not isinstance(options.shots, int) or isinstance(options.shots, bool) or options.shots < 1:
            raise QStackError("shots must be a positive integer")
        if formats and artifact.format.lower() not in formats:
            raise QStackError(f"{self.route} requires one of these artifact formats: {', '.join(sorted(formats))}", "UNSUPPORTED_FORMAT")

    def receipt(self, artifact, job_id, **metadata):
        if not job_id:
            raise QStackError(f"{self.route} returned no job identifier; do not resubmit automatically", "INVALID_RESPONSE")
        metadata.setdefault("num_clbits", artifact.physical_ir.get("num_clbits", 0))
        metadata.setdefault("measurement_mapping", artifact.physical_ir.get("measurement_mapping", []))
        return JobReceipt(route=self.route, target=artifact.target, job_id=str(job_id),
                          metadata=plain(metadata), artifact_hash=hashlib.sha256(artifact.program_bytes()).hexdigest())

    def result(self, receipt, raw, counts=None, **metadata):
        for key in ("num_clbits", "measurement_mapping"):
            if key in receipt.metadata:
                metadata.setdefault(key, receipt.metadata[key])
        metadata.setdefault("artifact_hash", receipt.artifact_hash)
        return ExecutionResult(route=self.route, target=receipt.target, job_id=receipt.job_id,
                               raw=plain(raw), counts=counts, metadata=plain(metadata))

    def check_auth(self):
        self.discover()
        return {"route": self.route, "authenticated": True}

    def login(self):
        return {"route": self.route, "interactive": False,
                "message": "Configure this provider's credentials, then run auth check."}

    def unsupported(self, capability, reason):
        raise QStackError(f"{self.route} {capability}: {reason}", "UNSUPPORTED_CAPABILITY")


class _NoRedirect(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        # Do not forward credential headers to a redirected origin.
        return None


class RestAdapter(Adapter):
    base_url = ""

    def headers(self):
        return {}

    def request(self, method, path, *, data=None, files=None, text=False):
        base = str(self.config.get("url") or self.config.get("host") or self.base_url).rstrip("/")
        parsed = urlsplit(base)
        if parsed.scheme != "https" or not parsed.netloc or parsed.username or parsed.password or parsed.query or parsed.fragment:
            raise QStackError("Provider URL must be an HTTPS origin/path without embedded credentials or query", "INVALID_CONFIG")
        if not path.startswith("/"):
            raise QStackError("Internal provider path must be relative")
        headers = {"Accept": "application/json", "User-Agent": "qstack/1", **self.headers()}
        body = None
        if files is not None:
            boundary = "qstack-" + uuid.uuid4().hex
            body = b""
            for name, value in files.items():
                value = value.encode("utf-8") if isinstance(value, str) else value
                body += (f'--{boundary}\r\nContent-Disposition: form-data; name="{name}"; filename="input.ll"\r\nContent-Type: application/octet-stream\r\n\r\n').encode() + value + b"\r\n"
            body += f"--{boundary}--\r\n".encode()
            headers["Content-Type"] = f"multipart/form-data; boundary={boundary}"
        elif data is not None:
            body = json.dumps(data, allow_nan=False).encode()
            headers["Content-Type"] = "application/json"
        transport = self.config.get("_transport")
        if transport is not None:
            return transport(method, base + path, headers=headers, data=data, files=files, text=text)
        try:
            request = Request(base + path, data=body, headers=headers, method=method)
            with build_opener(_NoRedirect()).open(request, timeout=float(self.config.get("timeout", 30))) as response:
                raw = response.read().decode("utf-8")
        except HTTPError as exc:
            # Vendor bodies can echo secrets/programs. Never include them in diagnostics.
            raise QStackError(f"{self.route} request failed (HTTP {exc.code}); submission was not retried", "PROVIDER_HTTP_ERROR") from None
        except URLError:
            raise QStackError(f"{self.route} connection failed; submission was not retried", "PROVIDER_CONNECTION_ERROR") from None
        if text:
            return raw
        if not raw.strip():
            return {}
        try:
            return json.loads(raw)
        except ValueError:
            raise QStackError(f"{self.route} returned invalid JSON", "INVALID_RESPONSE") from None


class UnavailableAdapter(Adapter):
    capabilities = {"submit": False, "status": False, "results": False, "cancel": False,
                    "discover": False, "login": False, "vendor_transpilation": None}

    def __init__(self, route, reason, config=None):
        super().__init__(config)
        self.route, self.reason = route, reason

    def check_auth(self):
        return {"route": self.route, "authenticated": False, "available": False, "reason": self.reason}

    def discover(self):
        return []

    def login(self):
        self.unsupported("login", self.reason)

    def submit(self, artifact, options):
        self.unsupported("submission", self.reason)

    def status(self, receipt):
        self.unsupported("status", self.reason)

    def results(self, receipt):
        self.unsupported("results", self.reason)

    def cancel(self, receipt):
        self.unsupported("cancel", self.reason)
