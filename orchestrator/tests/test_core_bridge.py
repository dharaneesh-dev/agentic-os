from __future__ import annotations

import subprocess
from pathlib import Path

from cooper_orchestrator import core_bridge
from cooper_orchestrator.config import GitSettings


def test_get_context_for_file_returns_source_text_for_existing_file(tmp_path: Path) -> None:
    (tmp_path / "sample.py").write_text("def add(a, b):\n    return a + b\n")

    context = core_bridge.get_context_for_file(str(tmp_path), "sample.py", token_budget=1000)

    assert "def add" in context
    assert "return a + b" in context


def test_get_context_for_file_returns_empty_string_for_missing_file(tmp_path: Path) -> None:
    context = core_bridge.get_context_for_file(str(tmp_path), "does_not_exist.py", token_budget=1000)

    assert context == ""


def test_get_context_for_file_respects_small_token_budget(tmp_path: Path) -> None:
    source = "def add(a, b):\n    return a + b\n\n\ndef subtract(a, b):\n    return a - b\n"
    (tmp_path / "sample.py").write_text(source)

    full_context = core_bridge.get_context_for_file(str(tmp_path), "sample.py", token_budget=1000)
    tiny_context = core_bridge.get_context_for_file(str(tmp_path), "sample.py", token_budget=1)

    assert len(tiny_context) <= len(full_context)


def test_get_context_for_file_returns_truncated_raw_content_for_non_python_file(tmp_path: Path) -> None:
    content = "x" * 100
    (tmp_path / "notes.txt").write_text(content)

    context = core_bridge.get_context_for_file(str(tmp_path), "notes.txt", token_budget=10)

    assert context == content[:40]


def test_get_context_for_file_uses_ast_chunking_not_raw_text_for_python_files(tmp_path: Path) -> None:
    source = "# top-level comment not part of any function\ndef add(a, b):\n    return a + b\n"
    (tmp_path / "sample.py").write_text(source)

    context = core_bridge.get_context_for_file(str(tmp_path), "sample.py", token_budget=1000)

    assert "def add" in context
    assert "top-level comment" not in context


def test_commit_change_creates_a_real_git_commit(tmp_path: Path) -> None:
    repo_path = tmp_path / "repo"
    repo_path.mkdir()
    subprocess.run(["git", "init", "-q"], cwd=repo_path, check=True)
    (repo_path / "file.py").write_text("value = 1\n")

    core_bridge.commit_change(
        str(repo_path),
        "initial commit from core_bridge",
        GitSettings(author_name="Cooper Test", author_email="cooper-test@local"),
    )

    log_output = subprocess.run(
        ["git", "log", "--oneline"],
        cwd=repo_path,
        check=True,
        capture_output=True,
        text=True,
    ).stdout

    assert "initial commit from core_bridge" in log_output


def test_commit_change_second_commit_has_a_parent(tmp_path: Path) -> None:
    repo_path = tmp_path / "repo"
    repo_path.mkdir()
    subprocess.run(["git", "init", "-q"], cwd=repo_path, check=True)
    git_settings = GitSettings(author_name="Cooper Test", author_email="cooper-test@local")

    (repo_path / "file.py").write_text("value = 1\n")
    core_bridge.commit_change(str(repo_path), "first commit", git_settings)

    (repo_path / "file.py").write_text("value = 2\n")
    core_bridge.commit_change(str(repo_path), "second commit", git_settings)

    log_output = subprocess.run(
        ["git", "log", "--oneline"],
        cwd=repo_path,
        check=True,
        capture_output=True,
        text=True,
    ).stdout

    assert log_output.count("\n") == 2
