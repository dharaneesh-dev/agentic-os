from __future__ import annotations

from cooper_orchestrator import core_bridge
from cooper_orchestrator.agents.coder import resolve_active_subtask
from cooper_orchestrator.config import Settings
from cooper_orchestrator.llm.client import LLMClient
from cooper_orchestrator.sandbox.docker_sandbox import DockerSandbox
from cooper_orchestrator.schemas import SuggestedFix, TaskState

_SYSTEM_PROMPT = (
    "You are the Tester agent in Cooper, an automated coding pipeline. A test "
    "run failed. Given the subtask, the code that was written, and the test "
    "failure output, diagnose the root cause and suggest a concrete fix."
)

def run(state: TaskState, llm: LLMClient, settings: Settings) -> TaskState:
    subtask = resolve_active_subtask(state)
    sandbox = DockerSandbox(settings.sandbox)
    test_result = sandbox.run(state.repo_path, ["python", "-m", "pytest", "-q"])

    if test_result.passed:
        core_bridge.commit_change(state.repo_path, subtask.description, settings.git)
        is_last_subtask = state.current_subtask_index >= len(state.ordered_subtask_ids) - 1
        if is_last_subtask:
            history_entry = f"tester: tests passed, committed change for subtask {subtask.id}"
            return state.model_copy(
                update={"test_result": test_result, "completed": True, "history": state.history + [history_entry]}
            )
        history_entry = (
            f"tester: tests passed, committed change for subtask {subtask.id}; advancing to next subtask"
        )
        return state.model_copy(
            update={
                "current_subtask_index": state.current_subtask_index + 1,
                "retry_count": 0,
                "test_result": None,
                "suggested_fix": None,
                "history": state.history + [history_entry],
            }
        )

    user_prompt = (
        f"Subtask: {subtask.description}\n\n"
        f"Code written (new_content):\n{state.coder_output.new_content if state.coder_output else ''}\n\n"
        f"Coder explanation: {state.coder_output.explanation if state.coder_output else ''}\n\n"
        f"Test stdout:\n{test_result.stdout}\n\nTest stderr:\n{test_result.stderr}"
    )
    suggested_fix: SuggestedFix = llm.complete(_SYSTEM_PROMPT, user_prompt, SuggestedFix)

    retry_count = state.retry_count + 1
    failed = retry_count >= state.max_retries
    history_entry = (
        f"tester: tests failed (retry {retry_count}/{state.max_retries}); suggested fix: {suggested_fix.diagnosis}"
    )
    return state.model_copy(
        update={
            "test_result": test_result,
            "retry_count": retry_count,
            "failed": failed,
            "suggested_fix": suggested_fix,
            "history": state.history + [history_entry],
        }
    )