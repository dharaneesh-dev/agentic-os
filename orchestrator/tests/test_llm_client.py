from __future__ import annotations

from unittest.mock import MagicMock, patch

import pytest
from pydantic import BaseModel, ValidationError

from cooper_orchestrator.config import Settings
from cooper_orchestrator.llm.client import FakeLLMClient, LiteLLMClient


class _Greeting(BaseModel):
    message: str


class _OtherModel(BaseModel):
    value: int


def test_fake_llm_client_returns_responses_in_order_from_list() -> None:
    first = _Greeting(message="hello")
    second = _Greeting(message="world")
    client = FakeLLMClient([first, second])

    assert client.complete("sys", "user", _Greeting) is first
    assert client.complete("sys", "user", _Greeting) is second


def test_fake_llm_client_returns_responses_keyed_by_call_order_from_dict() -> None:
    responses = {0: _Greeting(message="first"), 1: _Greeting(message="second")}
    client = FakeLLMClient(responses)

    assert client.complete("sys", "user", _Greeting).message == "first"
    assert client.complete("sys", "user", _Greeting).message == "second"


def test_fake_llm_client_raises_type_error_on_schema_mismatch() -> None:
    client = FakeLLMClient([_Greeting(message="hello")])

    with pytest.raises(TypeError):
        client.complete("sys", "user", _OtherModel)


def _mock_completion_response(content: str) -> MagicMock:
    response = MagicMock()
    response.choices = [MagicMock(message=MagicMock(content=content))]
    return response


def test_litellm_client_shapes_request_and_parses_valid_response() -> None:
    settings = Settings()
    client = LiteLLMClient(settings)
    valid_json = _Greeting(message="hi").model_dump_json()

    with patch("cooper_orchestrator.llm.client.litellm.completion") as mock_completion:
        mock_completion.return_value = _mock_completion_response(valid_json)
        result = client.complete("system prompt", "user prompt", _Greeting)

    assert result == _Greeting(message="hi")
    mock_completion.assert_called_once()
    _, kwargs = mock_completion.call_args
    assert kwargs["model"] == f"ollama/{settings.llm.model}"
    assert kwargs["api_base"] == settings.llm.base_url
    assert kwargs["format"] == "json"
    assert kwargs["timeout"] == settings.llm.request_timeout_seconds
    assert kwargs["messages"][0] == {"role": "system", "content": "system prompt"}
    assert kwargs["messages"][1]["role"] == "user"
    assert kwargs["messages"][1]["content"].startswith("user prompt")
    assert "JSON Schema" in kwargs["messages"][1]["content"]


def test_litellm_client_retries_once_on_invalid_json_then_succeeds() -> None:
    settings = Settings()
    client = LiteLLMClient(settings)
    valid_json = _Greeting(message="hi").model_dump_json()

    with patch("cooper_orchestrator.llm.client.litellm.completion") as mock_completion:
        mock_completion.side_effect = [
            _mock_completion_response("not valid json"),
            _mock_completion_response(valid_json),
        ]
        result = client.complete("system prompt", "user prompt", _Greeting)

    assert result == _Greeting(message="hi")
    assert mock_completion.call_count == 2
    second_call_kwargs = mock_completion.call_args_list[1].kwargs
    assert "invalid JSON for this schema" in second_call_kwargs["messages"][1]["content"]


def test_litellm_client_raises_after_max_attempts_exhausted() -> None:
    settings = Settings()
    client = LiteLLMClient(settings)

    with patch("cooper_orchestrator.llm.client.litellm.completion") as mock_completion:
        mock_completion.side_effect = [
            _mock_completion_response("still not valid"),
            _mock_completion_response("still not valid"),
        ]
        with pytest.raises(ValidationError):
            client.complete("system prompt", "user prompt", _Greeting)

    assert mock_completion.call_count == 2
