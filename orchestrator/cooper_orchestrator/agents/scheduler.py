from __future__ import annotations

from cooper_orchestrator.config import Settings
from cooper_orchestrator.llm.client import LLMClient
from cooper_orchestrator.schemas import TaskAllocation, TaskState

_SYSTEM_PROMPT = (
    "You are the Scheduler agent in Cooper. Given a list of subtasks, decide the "
    "order they should be implemented in - a subtask that others depend on must "
    "come first - and return a TaskAllocation listing every subtask id exactly "
    "once in ordered_subtask_ids, plus a short rationale."
)

def run(state: TaskState, llm: LLMClient, settings: Settings) -> TaskState:
    assert state.technical_spec is not None, "scheduler requires a technical_spec produced by product_manager"
    subtasks = state.technical_spec.subtasks
    original_order = [subtask.id for subtask in subtasks]

    subtask_lines = "\n".join(
        f"- id={subtask.id}, description={subtask.description}, target_files={subtask.target_files}"
        for subtask in subtasks
    )
    user_prompt = f"Subtasks:\n{subtask_lines}"

    allocation: TaskAllocation = llm.complete(_SYSTEM_PROMPT, user_prompt, TaskAllocation)

    if sorted(allocation.ordered_subtask_ids) == sorted(original_order):
        ordered_subtask_ids = allocation.ordered_subtask_ids
        history_entry = f"scheduler: ordered subtasks as {ordered_subtask_ids} ({allocation.rationale})"
    else:
        ordered_subtask_ids = original_order
        history_entry = (
            "scheduler: LLM returned an invalid ordering "
            f"{allocation.ordered_subtask_ids} (expected exactly {original_order}); "
            f"falling back to original subtask order {original_order}"
        )

    return state.model_copy(
        update={"ordered_subtask_ids": ordered_subtask_ids, "history": state.history + [history_entry]}
    )