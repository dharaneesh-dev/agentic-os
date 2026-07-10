from __future__ import annotations

import os
from pathlib import Path
from typing import Any

import yaml
from pydantic import BaseModel
from pydantic_settings import BaseSettings, PydanticBaseSettingsSource, SettingsConfigDict

class LLMSettings(BaseModel):
    base_url: str = "http://localhost:11434"
    model: str = "qwen3-coder:30b"
    request_timeout_seconds: int = 300

class SandboxSettings(BaseModel):
    image: str = "python:3.12-slim"
    timeout_seconds: int = 60
    memory_limit: str = "512m"
    network_disabled: bool = True

class GitSettings(BaseModel):
    author_name: str = "Cooper Agent"
    author_email: str = "cooper-agent@local"

def _resolve_config_path() -> Path | None:
    cwd_candidate = Path.cwd() / "config.yaml"
    if cwd_candidate.is_file():
        return cwd_candidate
    env_candidate = os.environ.get("COOPER_CONFIG_PATH")
    if env_candidate and Path(env_candidate).is_file():
        return Path(env_candidate)
    return None

class YamlConfigSettingsSource(PydanticBaseSettingsSource):
    def get_field_value(self, field: Any, field_name: str) -> tuple[Any, str, bool]:
        return None, field_name, False

    def __call__(self) -> dict[str, Any]:
        config_path = _resolve_config_path()
        if config_path is None:
            return {}
        with config_path.open("r") as config_file:
            loaded = yaml.safe_load(config_file)
        return loaded or {}

class Settings(BaseSettings):
    model_config = SettingsConfigDict(
        env_prefix="COOPER_",
        env_nested_delimiter="__",
        extra="ignore",
    )

    llm: LLMSettings = LLMSettings()
    sandbox: SandboxSettings = SandboxSettings()
    git: GitSettings = GitSettings()
    max_retries: int = 3

    @classmethod
    def settings_customise_sources(
        cls,
        settings_cls: type[BaseSettings],
        init_settings: PydanticBaseSettingsSource,
        env_settings: PydanticBaseSettingsSource,
        dotenv_settings: PydanticBaseSettingsSource,
        file_secret_settings: PydanticBaseSettingsSource,
    ) -> tuple[PydanticBaseSettingsSource, ...]:
        return (
            init_settings,
            env_settings,
            dotenv_settings,
            YamlConfigSettingsSource(settings_cls),
            file_secret_settings,
        )