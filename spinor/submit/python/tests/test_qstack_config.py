import io
import json
from pathlib import Path

import pytest

from qstack.config import resolve_config
from qstack.cli import main, _resolve
from qstack.models import QStackError


def test_precedence_and_env_path(tmp_path):
    config = tmp_path / "profiles.toml"
    config.write_text('[profiles.work]\nprovider="ibm"\ndevice="profile"\n')
    first = tmp_path / "first.env"
    second = tmp_path / "second file.env"
    first.write_text('QSTACK_IBM_DEVICE=first\nQSTACK_IBM_API_KEY="${LITERAL}"\n')
    second.write_text('QSTACK_IBM_DEVICE=second\n')
    args = {"config": str(config), "profile": "work", "env_file": [str(first), str(second)]}
    resolved = resolve_config("ibm", args, environ={}, cwd=tmp_path)
    assert resolved.values["device"] == "second"
    assert resolved.values["api_key"] == "${LITERAL}"
    assert resolved.redacted()["api_key"]["value"] == "<redacted>"
    assert resolve_config("ibm", args, environ={"QSTACK_IBM_DEVICE": "process"}, cwd=tmp_path).values["device"] == "process"
    args["device"] = "argument"
    assert resolve_config("ibm", args, environ={"QSTACK_IBM_DEVICE": "process"}, cwd=tmp_path).values["device"] == "argument"


def test_explicit_files_replace_cwd_and_do_not_search_parents(tmp_path):
    child = tmp_path / "child"
    child.mkdir()
    (tmp_path / ".env").write_text("QSTACK_IBM_API_KEY=parent-secret\n")
    assert "api_key" not in resolve_config("ibm", environ={}, cwd=child).values
    (child / ".env").write_text("QSTACK_IBM_API_KEY=cwd-secret\n")
    explicit = tmp_path / "explicit.env"
    explicit.write_text("QSTACK_IBM_DEVICE=device\n")
    assert "api_key" not in resolve_config("ibm", {"env_file": [explicit]}, environ={}, cwd=child).values
    assert "api_key" not in resolve_config("ibm", {"no_env_file": True}, environ={}, cwd=child).values


def test_later_dotenv_file_wins_even_when_using_legacy_alias(tmp_path):
    first, last = tmp_path / "first.env", tmp_path / "last.env"
    first.write_text("QSTACK_IBM_API_KEY=first\n")
    last.write_text("IBM_API_KEY=last\n")
    args = {"env_file": [str(first), str(last)]}
    resolved = resolve_config("ibm", args, environ={}, cwd=tmp_path)
    assert resolved.values["api_key"] == "last"
    assert resolved.sources["api_key"] == f"env-file:{last}"
    assert resolve_config("ibm", args, environ={"IBM_API_KEY": "process"}, cwd=tmp_path).values["api_key"] == "process"
    last.write_text("IBM_API_KEY=alias\nQSTACK_IBM_API_KEY=canonical\n")
    assert resolve_config("ibm", args, environ={}, cwd=tmp_path).values["api_key"] == "canonical"


def test_env_errors_do_not_echo_secrets(tmp_path):
    with pytest.raises(QStackError, match="Cannot read env file"):
        resolve_config("ibm", {"env_file": [tmp_path / "missing"]}, environ={})
    bad = tmp_path / "invalid.env"
    bad.write_text('API_KEY="very-secret\n')
    with pytest.raises(QStackError) as exc:
        resolve_config("ibm", {"env_file": [bad]}, environ={})
    assert "very-secret" not in str(exc.value)


def test_explicit_secret_and_stdin_override_environment(tmp_path):
    env = {"IBM_QUANTUM_TOKEN": "old-token"}
    assert resolve_config("ibm", {"api_key_stdin": True}, environ=env, cwd=tmp_path, stdin=io.StringIO("new-token\n")).values["api_key"] == "new-token"
    with pytest.raises(QStackError, match="exactly one"):
        resolve_config("ibm", {"api_key": "secret", "api_key_stdin": True}, environ={}, cwd=tmp_path)


def test_stdin_read_once(monkeypatch):
    monkeypatch.setattr("sys.stdin", io.StringIO("one-read"))
    resolved = _resolve({"provider": "ibm", "api_key_stdin": True, "no_env_file": True})
    assert resolved.values["api_key"] == "one-read"


def test_standard_provider_aliases_are_typed(tmp_path):
    aws = resolve_config("aws", environ={"AWS_ACCESS_KEY_ID": "id", "AWS_SECRET_ACCESS_KEY": "secret", "AWS_SESSION_TOKEN": "session"}, cwd=tmp_path)
    assert aws.values["access_key_id"] == "id"
    assert aws.values["secret_access_key"] == "secret"
    assert aws.values["session_token"] == "session"
    azure = resolve_config("azure", environ={"AZURE_QUANTUM_RESOURCE_ID": "/resource", "AZURE_CLIENT_SECRET": "credential"}, cwd=tmp_path)
    assert azure.values["workspace_resource_id"] == "/resource"
    assert azure.values["client_secret"] == "credential"


def test_configure_new_profile_and_resolve(tmp_path, capsys):
    path = tmp_path / "config.toml"
    # Config creation is allowed for auth configure, without overwriting another profile.
    path.write_text('[profiles.default]\nprovider="aws"\n')
    assert main(["auth", "configure", "--provider", "ibm", "--profile", "new", "--config", str(path), "--device", "ibm_fez", "--api-key", "transient-secret", "--no-env-file"]) == 0
    assert "transient-secret" not in path.read_text()
    assert "transient-secret" not in capsys.readouterr().out
    resolved = resolve_config(None, {"config": str(path), "profile": "new"}, environ={}, cwd=tmp_path)
    assert resolved.values["provider"] == "ibm"
    assert resolved.values["device"] == "ibm_fez"


def test_configure_preserves_environment_selected_profile(tmp_path, monkeypatch):
    import tomllib
    path = tmp_path / "config.toml"
    path.write_text('[profiles.default]\nprovider="aws"\n[profiles.work]\nprovider="ibm"\n')
    monkeypatch.setenv("QSTACK_CONFIG", str(path))
    monkeypatch.setenv("QSTACK_PROFILE", "work")
    assert main(["auth", "configure", "--device", "ibm_fez", "--no-env-file"]) == 0
    profiles = tomllib.loads(path.read_text())["profiles"]
    assert profiles["work"]["device"] == "ibm_fez"
    assert "device" not in profiles["default"]


def test_profile_roundtrips_nested_input_parameters(tmp_path):
    path = tmp_path / "nested.toml"
    params = {"arguments": [{"name": "angle", "value": 0.25, "type": "Double"}]}
    assert main(["auth", "configure", "--provider", "azure", "--config", str(path),
                 "--input-params", json.dumps(params), "--no-env-file"]) == 0
    resolved = resolve_config("azure", {"config": str(path)}, environ={}, cwd=tmp_path)
    assert resolved.values["input_params"] == params


@pytest.mark.parametrize("field,value", [("cost_cap_usd", "nan"), ("timeout", "inf"), ("shots", True), ("poll_interval", "-1")])
def test_invalid_numeric_configuration(field, value, tmp_path):
    with pytest.raises(QStackError):
        resolve_config("ibm", {field: value}, environ={}, cwd=tmp_path)


@pytest.mark.parametrize("field,value", [("shots", 1.9), ("seed", -1), ("timeout", True),
    ("max_cost_hqc", "nan"), ("max_cost_hqc", "-1"), ("input_params", "[]"),
    ("qubit_labels", '{}'), ("input_params", '{"angle": NaN}'), ("device", 123)])
def test_typed_fields_reject_lossy_or_wrong_type_values(field, value, tmp_path):
    with pytest.raises(QStackError):
        resolve_config("ibm", {field: value}, environ={}, cwd=tmp_path)


@pytest.mark.parametrize("route,field", [("iqm", "token"), ("oqc", "access_token"),
    ("azure", "client_secret"), ("aws", "secret_access_key")])
def test_all_provider_secret_files_and_stdin(route, field, tmp_path):
    path = tmp_path / "secret with spaces.txt"
    path.write_text("file-secret\n")
    env = {f"QSTACK_{route.upper()}_{field.upper()}": "environment-secret"}
    resolved = resolve_config(route, {field + "_file": path.name}, environ=env, cwd=tmp_path)
    assert resolved.values[field] == "file-secret"
    assert resolved.sources[field] == "credential-file"
    resolved = resolve_config(route, {field + "_stdin": True}, environ=env, cwd=tmp_path,
                              stdin=io.StringIO("stdin-secret\n"))
    assert resolved.values[field] == "stdin-secret"
    with pytest.raises(QStackError, match="exactly one"):
        resolve_config(route, {field: "argument-secret", field + "_file": str(path)}, environ={}, cwd=tmp_path)


def test_two_stdin_secrets_fail_without_consuming_input(tmp_path):
    stream = io.StringIO("unconsumed-secret")
    with pytest.raises(QStackError, match="Only one credential"):
        resolve_config("aws", {"access_key_id_stdin": True, "secret_access_key_stdin": True},
                       environ={}, cwd=tmp_path, stdin=stream)
    assert stream.tell() == 0


def test_generated_cli_options_share_field_types_aliases_and_help(tmp_path):
    from qstack.cli import parser
    from qstack.fields import FIELD_SPECS
    cli = parser()
    args = vars(cli.parse_args(["auth", "check", "--provider", "quantinuum", "--max-cost-hqc", "2.5",
                              "--password-stdin", "--no-env-file"]))
    resolved = resolve_config("quantinuum", args, environ={}, cwd=tmp_path, stdin=io.StringIO("secret"))
    assert resolved.values["max_cost_hqc"] == 2.5
    help_text = cli.format_help()
    assert "QSTACK_<ROUTE>_TOKEN" in help_text
    assert "IQM_TOKEN" in help_text
    assert all((spec.flags or ("--" + name.replace("_", "-"),))[0] in help_text
               for name, spec in FIELD_SPECS.items())


def test_token_stdin_read_once_after_route_inferred_from_environment(monkeypatch):
    monkeypatch.setenv("QSTACK_PROVIDER", "iqm")
    monkeypatch.setattr("sys.stdin", io.StringIO("one-read"))
    assert _resolve({"token_stdin": True, "no_env_file": True}).values["token"] == "one-read"


def test_successful_provider_response_redacts_echoed_secrets(monkeypatch, capsys):
    monkeypatch.setattr("qstack.cli.execute", lambda *args: {"message": "accepted echo-secret", "apiKey": "echo-secret"})
    assert main(["auth", "check", "--provider", "ionq", "--api-key", "echo-secret", "--no-env-file"]) == 0
    output = capsys.readouterr().out
    assert "echo-secret" not in output
    assert json.loads(output) == {"message": "accepted <redacted>"}


def test_config_show_keeps_redacted_secret_source(monkeypatch, capsys):
    monkeypatch.setenv("QSTACK_IONQ_API_KEY", "private-key")
    assert main(["config", "show", "--resolved", "--provider", "ionq", "--no-env-file"]) == 0
    output = json.loads(capsys.readouterr().out)
    assert output["api_key"] == {"value": "<redacted>", "source": "environment:QSTACK_IONQ_API_KEY"}
