from __future__ import annotations

from cooper_orchestrator.config import Settings
from cooper_orchestrator.llm.client import LLMClient
from cooper_orchestrator.schemas import TaskState, TechnicalSpec

_SYSTEM_PROMPT = (
    "You are the Product Manager agent in Cooper, an automated coding pipeline. "
    "Given a raw business requirement, produce a TechnicalSpec JSON object breaking "
    "the requirement into one or more concrete SubTasks. Each SubTask needs a unique "
    "id, a clear description of the code change to make, and the list of target_files "
    "(relative paths within the repository) it should modify or create."
)

def run(state: TaskState, llm: LLMClient, settings: Settings) -> TaskState:
    user_prompt = f"Business requirement:\n{state.business_requirement}"
    technical_spec: TechnicalSpec = llm.complete(_SYSTEM_PROMPT, user_prompt, TechnicalSpec)
    history_entry = f"product_manager: produced technical spec with {len(technical_spec.subtasks)} subtask(s)"
    return state.model_copy(update={"technical_spec": technical_spec, "history": state.history + [history_entry]})
