from __future__ import annotations

from pydantic import BaseModel, Field

class SubTask(BaseModel):
    id: str
    description: str
    target_files: list[str]

class TechnicalSpec(BaseModel):
    summary: str
    subtasks: list[SubTask]

class CoderOutput(BaseModel):
    file_path: str
    new_content: str
    explanation: str

class TestResult(BaseModel):
    passed: bool
    stdout: str
    stderr: str
    exit_code: int

class TaskAllocation(BaseModel):
    ordered_subtask_ids: list[str]
    rationale: str

class SuggestedFix(BaseModel):
    diagnosis: str
    suggested_fix: str

class TaskState(BaseModel):
    business_requirement: str
    repo_path: str
    technical_spec: TechnicalSpec | None = None
    ordered_subtask_ids: list[str] = Field(default_factory=list)
    current_subtask_index: int = 0
    coder_output: CoderOutput | None = None
    test_result: TestResult | None = None
    suggested_fix: SuggestedFix | None = None
    retry_count: int = 0
    max_retries: int = 3
    history: list[str] = Field(default_factory=list)
    failed: bool = False
    completed: bool = False