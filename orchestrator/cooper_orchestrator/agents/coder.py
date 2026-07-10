from __future__ import annotations

from pathlib import Path

from cooper_orchestrator import core_bridge
from cooper_orchestrator.config import Settings
from cooper_orchestrator.llm.client import LLMClient
from cooper_orchestrator.schemas import CoderOutput, SubTask, TaskState

_SYSTEM_PROMPT = (
    "You are the Coder agent in Cooper, an automated coding pipeline. Given a "
    "subtask description and the existing content of the target file (if any), "
    "produce a CoderOutput JSON object containing file_path, the FULL replacement "
    "content of that file (new_content, not a diff), and a short explanation of "
    "the change."
)

_CONTEXT_TOKEN_BUDGET = 2000

def resolve_active_subtask(state: TaskState) -> SubTask:
    assert state.technical_spec is not None, "coder requires a technical_spec"
    subtask_id = state.ordered_subtask_ids[state.current_subtask_index]
    for subtask in state.technical_spec.subtasks:
        if subtask.id == subtask_id:
            return subtask
    raise ValueError(f"no subtask with id {subtask_id!r} found in technical_spec")

def run(state: TaskState, llm: LLMClient, settings: Settings) -> TaskState:
    subtask = resolve_active_subtask(state)

    context_sections = [
        f"### {target_file}\n{core_bridge.get_context_for_file(state.repo_path, target_file, _CONTEXT_TOKEN_BUDGET)}"
        for target_file in subtask.target_files
    ]
    combined_context = "\n\n".join(context_sections)

    feedback_section = ""
    if state.test_result is not None and not state.test_result.passed:
        diagnosis_section = ""
        if state.suggested_fix is not None:
            diagnosis_section = (
                f"Diagnosis: {state.suggested_fix.diagnosis}\n"
                f"Suggested fix: {state.suggested_fix.suggested_fix}\n\n"
            )
        feedback_section = (
            "\n\nThe previous attempt failed the test suite. Fix the code so it "
            f"passes.\n{diagnosis_section}stdout:\n{state.test_result.stdout}\n\n"
            f"stderr:\n{state.test_result.stderr}"
        )

    user_prompt = (
        f"Business requirement:\n{state.business_requirement}\n\n"
        f"Subtask: {subtask.description}\n\n"
        f"Existing file context:\n{combined_context}"
        f"{feedback_section}\n\n"
        f"Target file: {subtask.target_files[0]}"
    )

    coder_output: CoderOutput = llm.complete(_SYSTEM_PROMPT, user_prompt, CoderOutput)

    target_path = Path(state.repo_path) / coder_output.file_path
    target_path.parent.mkdir(parents=True, exist_ok=True)
    target_path.write_text(coder_output.new_content)

    history_entry = f"coder: wrote {coder_output.file_path} ({coder_output.explanation})"
    return state.model_copy(update={"coder_output": coder_output, "history": state.history + [history_entry]})