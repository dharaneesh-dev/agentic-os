from __future__ import annotations

from typing import Protocol

import litellm
from pydantic import BaseModel, ValidationError

from cooper_orchestrator.config import Settings

_MAX_ATTEMPTS = 2

class LLMClient(Protocol):
    def complete(self, system_prompt: str, user_prompt: str, response_model: type[BaseModel]) -> BaseModel: ...

class LiteLLMClient:
    def __init__(self, settings: Settings) -> None:
        self._settings = settings

    def complete(self, system_prompt: str, user_prompt: str, response_model: type[BaseModel]) -> BaseModel:
        schema_instruction = (
            "\n\nRespond with a single JSON object matching exactly this JSON Schema "
            "(top-level keys only, do not nest it under the schema or model name):\n"
            f"{response_model.model_json_schema()}"
        )
        current_user_prompt = user_prompt + schema_instruction
        last_error: ValidationError | None = None
        for _ in range(_MAX_ATTEMPTS):
            raw_content = self._request(system_prompt, current_user_prompt)
            try:
                return response_model.model_validate_json(raw_content)
            except ValidationError as validation_error:
                last_error = validation_error
                current_user_prompt = (
                    f"{user_prompt}{schema_instruction}\n\nYour previous response was invalid JSON for this "
                    f"schema: {validation_error}. Try again."
                )
        assert last_error is not None
        raise last_error

    def _request(self, system_prompt: str, user_prompt: str) -> str:
        response = litellm.completion(
            model=f"ollama/{self._settings.llm.model}",
            api_base=self._settings.llm.base_url,
            messages=[
                {"role": "system", "content": system_prompt},
                {"role": "user", "content": user_prompt},
            ],
            format="json",
            timeout=self._settings.llm.request_timeout_seconds,
        )
        return response.choices[0].message.content

class FakeLLMClient:
    def __init__(self, responses: list[BaseModel] | dict[int, BaseModel]) -> None:
        self._responses = responses
        self._call_count = 0

    def complete(self, system_prompt: str, user_prompt: str, response_model: type[BaseModel]) -> BaseModel:
        response = self._responses[self._call_count]
        self._call_count += 1
        if not isinstance(response, response_model):
            raise TypeError(f"FakeLLMClient call {self._call_count} expected {response_model}, got {type(response)}")
        return response
