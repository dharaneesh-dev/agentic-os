from __future__ import annotations

import subprocess
from pathlib import Path

from cooper_orchestrator.agents import coder, product_manager, scheduler, tester
from cooper_orchestrator.config import Settings
from cooper_orchestrator.llm.client import FakeLLMClient
from cooper_orchestrator.schemas import (
    CoderOutput,
    SubTask,
    SuggestedFix,
    TaskAllocation,
    TaskState,
    TechnicalSpec,
    TestResult,
)

_CORRECT_CALCULATOR_CONTENT = (
    "def add(a, b):\n"
    "    return a + b\n"
    "\n\n"
    "def subtract(a, b):\n"
    "    return a - b\n"
)

_BUGGY_CALCULATOR_CONTENT = (
    "def add(a, b):\n"
    "    return a + b\n"
    "\n\n"
    "def subtract(a, b):\n"
    "    return a + b\n"
)


def test_product_manager_populates_technical_spec_and_records_history() -> None:
    spec = TechnicalSpec(
        summary="Add subtract",
        subtasks=[SubTask(id="1", description="add subtract fn", target_files=["calculator.py"])],
    )
    llm = FakeLLMClient([spec])
    state = TaskState(business_requirement="add a subtract function", repo_path="/tmp/does-not-matter")
    settings = Settings()

    new_state = product_manager.run(state, llm, settings)

    assert new_state.technical_spec == spec
    assert len(new_state.history) == 1
    assert new_state.history[0].startswith("product_manager:")
    assert state.history == []


def test_scheduler_uses_llm_ordering_when_valid() -> None:
    spec = TechnicalSpec(
        summary="Add features",
        subtasks=[
            SubTask(id="1", description="add base util", target_files=["utils.py"]),
            SubTask(id="2", description="add feature using util", target_files=["feature.py"]),
        ],
    )
    allocation = TaskAllocation(ordered_subtask_ids=["1", "2"], rationale="util must exist before feature uses it")
    state = TaskState(business_requirement="req", repo_path="/tmp/x", technical_spec=spec)
    settings = Settings()

    new_state = scheduler.run(state, FakeLLMClient([allocation]), settings)

    assert new_state.ordered_subtask_ids == ["1", "2"]
    assert new_state.history[0].startswith("scheduler:")
    assert "util must exist before feature uses it" in new_state.history[0]


def test_scheduler_falls_back_to_original_order_when_llm_ordering_is_invalid() -> None:
    spec = TechnicalSpec(
        summary="Add features",
        subtasks=[
            SubTask(id="1", description="add base util", target_files=["utils.py"]),
            SubTask(id="2", description="add feature using util", target_files=["feature.py"]),
        ],
    )
    # invalid: duplicates "1" and omits "2" entirely
    allocation = TaskAllocation(ordered_subtask_ids=["1", "1"], rationale="oops")
    state = TaskState(business_requirement="req", repo_path="/tmp/x", technical_spec=spec)
    settings = Settings()

    new_state = scheduler.run(state, FakeLLMClient([allocation]), settings)

    assert new_state.ordered_subtask_ids == ["1", "2"]
    assert new_state.history[0].startswith("scheduler:")
    assert "falling back" in new_state.history[0].lower()


def test_coder_writes_new_content_to_target_file(golden_repo_path: str) -> None:
    spec = TechnicalSpec(
        summary="Add subtract",
        subtasks=[SubTask(id="1", description="add subtract fn", target_files=["calculator.py"])],
    )
    coder_output = CoderOutput(
        file_path="calculator.py",
        new_content=_CORRECT_CALCULATOR_CONTENT,
        explanation="added subtract",
    )
    llm = FakeLLMClient([coder_output])
    state = TaskState(
        business_requirement="add subtract",
        repo_path=golden_repo_path,
        technical_spec=spec,
        ordered_subtask_ids=["1"],
    )
    settings = Settings()

    new_state = coder.run(state, llm, settings)

    assert new_state.coder_output == coder_output
    assert (Path(golden_repo_path) / "calculator.py").read_text() == _CORRECT_CALCULATOR_CONTENT
    assert new_state.history[0].startswith("coder:")


def test_coder_creates_parent_directories_for_new_files(tmp_path: Path) -> None:
    coder_output = CoderOutput(file_path="pkg/new_module.py", new_content="x = 1\n", explanation="new file")
    llm = FakeLLMClient([coder_output])
    spec = TechnicalSpec(
        summary="new module",
        subtasks=[SubTask(id="1", description="create new module", target_files=["pkg/new_module.py"])],
    )
    state = TaskState(
        business_requirement="req",
        repo_path=str(tmp_path),
        technical_spec=spec,
        ordered_subtask_ids=["1"],
    )
    settings = Settings()

    coder.run(state, llm, settings)

    assert (tmp_path / "pkg" / "new_module.py").read_text() == "x = 1\n"


def test_coder_includes_prior_test_failure_feedback_in_prompt(golden_repo_path: str) -> None:
    class _RecordingFakeLLMClient(FakeLLMClient):
        def __init__(self, responses):
            super().__init__(responses)
            self.seen_prompts: list[str] = []

        def complete(self, system_prompt, user_prompt, response_model):
            self.seen_prompts.append(user_prompt)
            return super().complete(system_prompt, user_prompt, response_model)

    coder_output = CoderOutput(
        file_path="calculator.py", new_content=_CORRECT_CALCULATOR_CONTENT, explanation="retry fix"
    )
    llm = _RecordingFakeLLMClient([coder_output])
    spec = TechnicalSpec(
        summary="Add subtract",
        subtasks=[SubTask(id="1", description="add subtract fn", target_files=["calculator.py"])],
    )
    failed_result = TestResult(passed=False, stdout="1 failed", stderr="AssertionError: subtract missing", exit_code=1)
    state = TaskState(
        business_requirement="add subtract",
        repo_path=golden_repo_path,
        technical_spec=spec,
        ordered_subtask_ids=["1"],
        test_result=failed_result,
        retry_count=1,
    )
    settings = Settings()

    coder.run(state, llm, settings)

    assert "AssertionError: subtract missing" in llm.seen_prompts[0]


def test_coder_leads_feedback_with_suggested_fix_when_present(golden_repo_path: str) -> None:
    class _RecordingFakeLLMClient(FakeLLMClient):
        def __init__(self, responses):
            super().__init__(responses)
            self.seen_prompts: list[str] = []

        def complete(self, system_prompt, user_prompt, response_model):
            self.seen_prompts.append(user_prompt)
            return super().complete(system_prompt, user_prompt, response_model)

    coder_output = CoderOutput(
        file_path="calculator.py", new_content=_CORRECT_CALCULATOR_CONTENT, explanation="applied suggested fix"
    )
    llm = _RecordingFakeLLMClient([coder_output])
    spec = TechnicalSpec(
        summary="Add subtract",
        subtasks=[SubTask(id="1", description="add subtract fn", target_files=["calculator.py"])],
    )
    failed_result = TestResult(passed=False, stdout="1 failed", stderr="AssertionError: subtract missing", exit_code=1)
    suggested_fix = SuggestedFix(diagnosis="subtract is missing", suggested_fix="add a subtract(a, b) function")
    state = TaskState(
        business_requirement="add subtract",
        repo_path=golden_repo_path,
        technical_spec=spec,
        ordered_subtask_ids=["1"],
        test_result=failed_result,
        suggested_fix=suggested_fix,
        retry_count=1,
    )
    settings = Settings()

    coder.run(state, llm, settings)

    prompt = llm.seen_prompts[0]
    assert prompt.index("subtract is missing") < prompt.index("AssertionError: subtract missing")


def test_tester_suggests_fix_and_increments_retry_count_when_tests_fail(
    golden_repo_path: str, test_settings: Settings
) -> None:
    spec = TechnicalSpec(
        summary="Add subtract",
        subtasks=[SubTask(id="1", description="add subtract fn", target_files=["calculator.py"])],
    )
    coder_output = CoderOutput(file_path="calculator.py", new_content=_BUGGY_CALCULATOR_CONTENT, explanation="buggy")
    suggested_fix = SuggestedFix(diagnosis="subtract adds instead of subtracting", suggested_fix="use a - b")
    state = TaskState(
        business_requirement="req",
        repo_path=golden_repo_path,
        technical_spec=spec,
        ordered_subtask_ids=["1"],
        coder_output=coder_output,
        retry_count=0,
        max_retries=3,
    )

    new_state = tester.run(state, FakeLLMClient([suggested_fix]), test_settings)

    assert new_state.test_result is not None
    assert new_state.test_result.passed is False
    assert new_state.retry_count == 1
    assert new_state.failed is False
    assert new_state.completed is False
    assert new_state.suggested_fix == suggested_fix


def test_tester_sets_failed_once_max_retries_reached(golden_repo_path: str, test_settings: Settings) -> None:
    spec = TechnicalSpec(
        summary="Add subtract",
        subtasks=[SubTask(id="1", description="add subtract fn", target_files=["calculator.py"])],
    )
    coder_output = CoderOutput(file_path="calculator.py", new_content=_BUGGY_CALCULATOR_CONTENT, explanation="buggy")
    suggested_fix = SuggestedFix(diagnosis="still wrong", suggested_fix="use a - b")
    state = TaskState(
        business_requirement="req",
        repo_path=golden_repo_path,
        technical_spec=spec,
        ordered_subtask_ids=["1"],
        coder_output=coder_output,
        retry_count=2,
        max_retries=3,
    )

    new_state = tester.run(state, FakeLLMClient([suggested_fix]), test_settings)

    assert new_state.retry_count == 3
    assert new_state.failed is True
    assert new_state.completed is False


def test_tester_advances_to_next_subtask_on_success_when_not_last(
    golden_repo_path: str, test_settings: Settings
) -> None:
    (Path(golden_repo_path) / "calculator.py").write_text(_CORRECT_CALCULATOR_CONTENT)
    spec = TechnicalSpec(
        summary="Add subtract and multiply",
        subtasks=[
            SubTask(id="1", description="add subtract fn", target_files=["calculator.py"]),
            SubTask(id="2", description="add multiply fn", target_files=["calculator.py"]),
        ],
    )
    state = TaskState(
        business_requirement="req",
        repo_path=golden_repo_path,
        technical_spec=spec,
        ordered_subtask_ids=["1", "2"],
        current_subtask_index=0,
        suggested_fix=SuggestedFix(diagnosis="d", suggested_fix="f"),
        retry_count=2,
    )

    new_state = tester.run(state, FakeLLMClient([]), test_settings)

    assert new_state.current_subtask_index == 1
    assert new_state.retry_count == 0
    assert new_state.test_result is None
    assert new_state.suggested_fix is None
    assert new_state.completed is False
    assert new_state.failed is False

    log_output = subprocess.run(
        ["git", "log", "--oneline"],
        cwd=golden_repo_path,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    assert "add subtract fn" in log_output


def test_tester_marks_completed_and_commits_when_tests_pass(
    golden_repo_path: str, test_settings: Settings
) -> None:
    (Path(golden_repo_path) / "calculator.py").write_text(_CORRECT_CALCULATOR_CONTENT)
    spec = TechnicalSpec(
        summary="fixed calculator via tester test",
        subtasks=[SubTask(id="1", description="add subtract fn to calculator", target_files=["calculator.py"])],
    )
    state = TaskState(
        business_requirement="req",
        repo_path=golden_repo_path,
        technical_spec=spec,
        ordered_subtask_ids=["1"],
    )

    new_state = tester.run(state, FakeLLMClient([]), test_settings)

    assert new_state.test_result is not None
    assert new_state.test_result.passed is True
    assert new_state.completed is True

    log_output = subprocess.run(
        ["git", "log", "--oneline"],
        cwd=golden_repo_path,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    assert "add subtract fn to calculator" in log_output
