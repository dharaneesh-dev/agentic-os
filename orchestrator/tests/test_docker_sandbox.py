from __future__ import annotations

from pathlib import Path

from cooper_orchestrator.config import SandboxSettings
from cooper_orchestrator.sandbox.docker_sandbox import DockerSandbox


def test_sandbox_runs_trivial_successful_command(tmp_path: Path) -> None:
    sandbox = DockerSandbox(SandboxSettings(timeout_seconds=30))

    result = sandbox.run(str(tmp_path), ["python", "-c", "print('hello from sandbox')"])

    assert result.passed is True
    assert result.exit_code == 0
    assert "hello from sandbox" in result.stdout


def test_sandbox_reports_nonzero_exit_code_as_failure(tmp_path: Path) -> None:
    sandbox = DockerSandbox(SandboxSettings(timeout_seconds=30))

    result = sandbox.run(str(tmp_path), ["python", "-c", "import sys; sys.exit(7)"])

    assert result.passed is False
    assert result.exit_code == 7


def test_sandbox_captures_stderr_separately(tmp_path: Path) -> None:
    sandbox = DockerSandbox(SandboxSettings(timeout_seconds=30))

    result = sandbox.run(
        str(tmp_path),
        ["python", "-c", "import sys; print('to stdout'); print('to stderr', file=sys.stderr)"],
    )

    assert "to stdout" in result.stdout
    assert "to stderr" in result.stderr


def test_sandbox_can_read_mounted_repo_files(tmp_path: Path) -> None:
    (tmp_path / "marker.txt").write_text("cooper-marker-42")
    sandbox = DockerSandbox(SandboxSettings(timeout_seconds=30))

    result = sandbox.run(str(tmp_path), ["cat", "marker.txt"])

    assert result.passed is True
    assert "cooper-marker-42" in result.stdout


def test_sandbox_times_out_on_long_running_command(tmp_path: Path) -> None:
    sandbox = DockerSandbox(SandboxSettings(timeout_seconds=2))

    result = sandbox.run(str(tmp_path), ["sleep", "30"])

    assert result.passed is False
    assert result.exit_code == -1
    assert "timed out" in result.stderr


def test_sandbox_does_not_leak_containers(tmp_path: Path) -> None:
    import docker

    client = docker.from_env()
    before = len(client.containers.list(all=True))

    sandbox = DockerSandbox(SandboxSettings(timeout_seconds=10))
    sandbox.run(str(tmp_path), ["python", "-c", "print('cleanup check')"])

    after = len(client.containers.list(all=True))
    assert after == before
