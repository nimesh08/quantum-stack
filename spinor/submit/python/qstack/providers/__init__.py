"""Optional, explicit-auth provider adapters. Importing this module has no network effects."""
from qstack.models import QStackError
from .aqt import AQTAdapter
from .base import UnavailableAdapter
from .cloud import AWSAdapter, AzureAdapter, GoogleAdapter, IBMAdapter
from .rest import AliceBobAdapter, AnyonAdapter, IonQAdapter
from .qibolab import QibolabAdapter
from .specialized import IQMAdapter, OQCAdapter, QuantinuumAdapter, RigettiAdapter
from .serialization import validate_serialization

ADAPTERS = {cls.route: cls for cls in (IBMAdapter, GoogleAdapter, QuantinuumAdapter, AzureAdapter,
    AWSAdapter, IonQAdapter, RigettiAdapter, IQMAdapter, OQCAdapter, AQTAdapter, AnyonAdapter, AliceBobAdapter, QibolabAdapter)}

UNAVAILABLE = {
    "qci": "Quantum Circuits Inc Aqumen has no verified public raw submission/auth contract; qci-client belongs to a different company",
    "tii": "The TII Falcon cloud endpoint and authentication contract are not publicly verified",
}


def get_adapter(route: str, config: dict | None = None):
    route = route.lower()
    if route in ADAPTERS:
        adapter = ADAPTERS[route](config)
        from .isolated import ENV_FIELDS, IsolatedSDKAdapter
        if not adapter.config.get("_isolated_sdk_worker") and not adapter.config.get("_client") and any(
                adapter.config.get(field) for field in ENV_FIELDS.get(route, {})):
            return IsolatedSDKAdapter(adapter)
        return adapter
    if route in UNAVAILABLE:
        return UnavailableAdapter(route, UNAVAILABLE[route], config)
    raise QStackError(f"Unknown provider route '{route}'", "UNKNOWN_PROVIDER")


__all__ = ["get_adapter", "ADAPTERS", "UNAVAILABLE", "validate_serialization"]
