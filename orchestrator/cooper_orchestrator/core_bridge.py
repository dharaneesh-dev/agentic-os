from __future__ import annotations

import sys
from pathlib import Path

_CORE_BUILD_DIR = Path(__file__).resolve().parents[2] / "core" / "build"
if str(_CORE_BUILD_DIR) not in sys.path:
    sys.path.insert(0, str(_CORE_BUILD_DIR))

try:
    import cooper_core
except ImportError as import_error:
    raise RuntimeError(
        "Failed to import cooper_core. Build the C++ core first with:\n"
        "  cmake -S core -B core/build -DCOOPER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release\n"
        "  cmake --build core/build -j\n"
        f"(looked for the extension module under {_CORE_BUILD_DIR})"
    ) from import_error

from cooper_orchestrator.config import GitSettings

def get_context_for_file(repo_path: str, file_path: str, token_budget: int) -> str:
    absolute_path = Path(repo_path) / file_path
    if not absolute_path.is_file():
        return ""
    if absolute_path.suffix != ".py":
        # token_budget * 4 mirrors ContextPacker::EstimateTokens's text.size() / 4 heuristic in C++
        return absolute_path.read_text()[: token_budget * 4]
    parser = cooper_core.Parser()
    chunks = parser.parse_file(str(absolute_path))
    packer = cooper_core.ContextPacker()
    packed = packer.pack(chunks, token_budget)
    return "\n\n".join(chunk.source_text for chunk in packed.included_chunks)

def commit_change(repo_path: str, message: str, git_settings: GitSettings) -> None:
    repository = cooper_core.Repository.open(repo_path)
    repository.stage_all()
    repository.commit(message, git_settings.author_name, git_settings.author_email)
