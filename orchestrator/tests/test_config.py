from __future__ import annotations

from pathlib import Path

import pytest

from cooper_orchestrator.config import Settings


def test_defaults_are_used_when_no_yaml_or_env_present(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("COOPER_CONFIG_PATH", raising=False)

    settings = Settings()

    assert settings.llm.base_url == "http://localhost:11434"
    assert settings.llm.model == "qwen3-coder:30b"
    assert settings.sandbox.image == "python:3.12-slim"
    assert settings.git.author_name == "Cooper Agent"
    assert settings.max_retries == 3


def test_yaml_file_in_cwd_overrides_defaults(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.chdir(tmp_path)
    monkeypatch.delenv("COOPER_CONFIG_PATH", raising=False)
    (tmp_path / "config.yaml").write_text(
        "llm:\n  base_url: http://remote-ollama:11434\n  model: custom-model\nmax_retries: 7\n"
    )

    settings = Settings()

    assert settings.llm.base_url == "http://remote-ollama:11434"
    assert settings.llm.model == "custom-model"
    assert settings.max_retries == 7


def test_yaml_file_via_env_var_path_is_used(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    empty_cwd = tmp_path / "cwd"
    empty_cwd.mkdir()
    monkeypatch.chdir(empty_cwd)
    config_path = tmp_path / "elsewhere.yaml"
    config_path.write_text("git:\n  author_name: Someone Else\n")
    monkeypatch.setenv("COOPER_CONFIG_PATH", str(config_path))

    settings = Settings()

    assert settings.git.author_name == "Someone Else"


def test_env_var_overrides_yaml(monkeypatch: pytest.MonkeyPatch, tmp_path: Path) -> None:
    monkeypatch.chdir(tmp_path)
    (tmp_path / "config.yaml").write_text("llm:\n  model: from-yaml\n")
    monkeypatch.setenv("COOPER_LLM__MODEL", "from-env")

    settings = Settings()

    assert settings.llm.model == "from-env"
