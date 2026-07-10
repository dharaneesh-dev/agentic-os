from __future__ import annotations

import subprocess
from pathlib import Path

from cooper_orchestrator.config import Settings
from cooper_orchestrator.core_bridge import cooper_core
from cooper_orchestrator.graph import run_pipeline
from cooper_orchestrator.llm.client import FakeLLMClient
from cooper_orchestrator.schemas import CoderOutput, SubTask, SuggestedFix, TaskAllocation, TechnicalSpec

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

_CALCULATOR_WITH_MULTIPLY_CONTENT = _CORRECT_CALCULATOR_CONTENT + "\n\ndef multiply(a, b):\n    return a * b\n"


def test_golden_path_pipeline_reaches_completion(
    golden_repo_path: str, test_settings: Settings, golden_brd_text: str
) -> None:
    technical_spec = TechnicalSpec(
        summary="Add subtract(a, b) to calculator.py",
        subtasks=[
            SubTask(
                id="1",
                description="Add a subtract(a, b) function to calculator.py that returns a - b",
                target_files=["calculator.py"],
            )
        ],
    )
    allocation = TaskAllocation(ordered_subtask_ids=["1"], rationale="only one subtask to schedule")
    coder_output = CoderOutput(
        file_path="calculator.py",
        new_content=_CORRECT_CALCULATOR_CONTENT,
        explanation="Added the missing subtract function",
    )
    llm = FakeLLMClient([technical_spec, allocation, coder_output])

    final_state = run_pipeline(golden_brd_text, golden_repo_path, llm, test_settings)

    assert final_state.completed is True
    assert final_state.failed is False
    assert final_state.test_result is not None
    assert final_state.test_result.passed is True
    assert final_state.test_result.exit_code == 0

    log_output = subprocess.run(
        ["git", "log", "--oneline"],
        cwd=golden_repo_path,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    commit_lines = [line for line in log_output.strip().splitlines() if line]
    assert len(commit_lines) == 1
    assert "subtract" in log_output

    repository = cooper_core.Repository.open(golden_repo_path)
    assert repository.current_branch() in ("main", "master")


def test_two_subtask_requirement_completes_via_real_docker_sandbox(
    golden_repo_path: str, test_settings: Settings
) -> None:
    technical_spec = TechnicalSpec(
        summary="Add subtract and multiply to calculator.py",
        subtasks=[
            SubTask(id="1", description="Add subtract(a, b) to calculator.py", target_files=["calculator.py"]),
            SubTask(id="2", description="Add multiply(a, b) to calculator.py", target_files=["calculator.py"]),
        ],
    )
    allocation = TaskAllocation(ordered_subtask_ids=["1", "2"], rationale="subtract has no dependency on multiply")
    coder_output_subtract = CoderOutput(
        file_path="calculator.py",
        new_content=_CORRECT_CALCULATOR_CONTENT,
        explanation="added subtract",
    )
    coder_output_multiply = CoderOutput(
        file_path="calculator.py",
        new_content=_CALCULATOR_WITH_MULTIPLY_CONTENT,
        explanation="added multiply, building on the subtract implementation",
    )
    llm = FakeLLMClient([technical_spec, allocation, coder_output_subtract, coder_output_multiply])

    final_state = run_pipeline(
        "Add subtract and multiply functions to calculator.py", golden_repo_path, llm, test_settings
    )

    assert final_state.completed is True
    assert final_state.failed is False
    assert final_state.current_subtask_index == 1

    final_content = (Path(golden_repo_path) / "calculator.py").read_text()
    namespace: dict[str, object] = {}
    exec(final_content, namespace)  # noqa: S102 - verifying generated code actually works
    assert namespace["subtract"](5, 3) == 2
    assert namespace["multiply"](4, 3) == 12

    log_output = subprocess.run(
        ["git", "log", "--oneline"],
        cwd=golden_repo_path,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    commit_lines = [line for line in log_output.strip().splitlines() if line]
    assert len(commit_lines) == 2


def test_bug_found_then_suggested_fix_then_retest_passes(
    golden_repo_path: str, test_settings: Settings
) -> None:
    technical_spec = TechnicalSpec(
        summary="Add subtract(a, b) to calculator.py",
        subtasks=[
            SubTask(
                id="1",
                description="Add a subtract(a, b) function to calculator.py that returns a - b",
                target_files=["calculator.py"],
            )
        ],
    )
    allocation = TaskAllocation(ordered_subtask_ids=["1"], rationale="only one subtask to schedule")
    buggy_coder_output = CoderOutput(
        file_path="calculator.py",
        new_content=_BUGGY_CALCULATOR_CONTENT,
        explanation="added subtract (implemented as addition, incorrectly)",
    )
    suggested_fix = SuggestedFix(
        diagnosis="subtract(a, b) is implemented as a + b instead of a - b",
        suggested_fix="change `return a + b` in subtract to `return a - b`",
    )
    fixed_coder_output = CoderOutput(
        file_path="calculator.py",
        new_content=_CORRECT_CALCULATOR_CONTENT,
        explanation="fixed subtract to actually subtract",
    )
    llm = FakeLLMClient([technical_spec, allocation, buggy_coder_output, suggested_fix, fixed_coder_output])

    final_state = run_pipeline(
        "Add a subtract function to calculator.py", golden_repo_path, llm, test_settings
    )

    assert final_state.retry_count == 1
    assert final_state.suggested_fix == suggested_fix
    assert final_state.test_result is not None
    assert final_state.test_result.passed is True
    assert final_state.completed is True
    assert final_state.failed is False

    log_output = subprocess.run(
        ["git", "log", "--oneline"],
        cwd=golden_repo_path,
        check=True,
        capture_output=True,
        text=True,
    ).stdout
    commit_lines = [line for line in log_output.strip().splitlines() if line]
    assert len(commit_lines) == 1
