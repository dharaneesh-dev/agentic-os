from __future__ import annotations

import pytest
from pydantic import ValidationError

from cooper_orchestrator.schemas import (
    CoderOutput,
    SubTask,
    SuggestedFix,
    TaskAllocation,
    TaskState,
    TechnicalSpec,
    TestResult,
)


def test_subtask_round_trip() -> None:
    subtask = SubTask(id="1", description="do the thing", target_files=["a.py", "b.py"])
    assert SubTask.model_validate_json(subtask.model_dump_json()) == subtask


def test_technical_spec_requires_subtasks_field() -> None:
    with pytest.raises(ValidationError):
        TechnicalSpec.model_validate({"summary": "no subtasks key"})


def test_coder_output_round_trip() -> None:
    output = CoderOutput(file_path="calculator.py", new_content="def add(a, b):\n    return a + b\n", explanation="x")
    assert CoderOutput.model_validate_json(output.model_dump_json()) == output


def test_test_result_round_trip() -> None:
    result = TestResult(passed=True, stdout="ok", stderr="", exit_code=0)
    assert TestResult.model_validate_json(result.model_dump_json()) == result


def test_task_allocation_round_trip() -> None:
    allocation = TaskAllocation(ordered_subtask_ids=["1", "2"], rationale="1 must come before 2")
    assert TaskAllocation.model_validate_json(allocation.model_dump_json()) == allocation


def test_suggested_fix_round_trip() -> None:
    fix = SuggestedFix(diagnosis="off-by-one in subtract", suggested_fix="use a - b instead of b - a")
    assert SuggestedFix.model_validate_json(fix.model_dump_json()) == fix


def test_task_state_defaults() -> None:
    state = TaskState(business_requirement="add subtract", repo_path="/tmp/repo")
    assert state.technical_spec is None
    assert state.ordered_subtask_ids == []
    assert state.current_subtask_index == 0
    assert state.suggested_fix is None
    assert state.retry_count == 0
    assert state.max_retries == 3
    assert state.history == []
    assert state.failed is False
    assert state.completed is False


def test_task_state_history_is_append_only_via_copy() -> None:
    state = TaskState(business_requirement="req", repo_path="/tmp/repo")
    updated = state.model_copy(update={"history": state.history + ["step one"]})

    assert state.history == []
    assert updated.history == ["step one"]
