from __future__ import annotations

import docker
from docker.errors import APIError, ContainerError, ImageNotFound
from docker.models.containers import Container

from cooper_orchestrator.config import SandboxSettings
from cooper_orchestrator.schemas import TestResult

class DockerSandbox:
    def __init__(self, settings: SandboxSettings) -> None:
        self._settings = settings
        self._client = docker.from_env()

    def run(self, repo_path: str, command: list[str]) -> TestResult:
        try:
            container = self._create_container(repo_path, command)
        except ImageNotFound:
            self._client.images.pull(self._settings.image)
            container = self._create_container(repo_path, command)
        try:
            container.start()
            try:
                wait_result = container.wait(timeout=self._settings.timeout_seconds)
                exit_code = wait_result.get("StatusCode", -1)
            except Exception:
                container.kill()
                return TestResult(passed=False, stdout="", stderr="sandbox timed out", exit_code=-1)

            # two separate non-streaming logs() calls, one per stream, rely on
            # the SDK demultiplexing the container's combined log correctly;
            # this only works because the container is created without a tty.
            stdout_bytes = container.logs(stdout=True, stderr=False)
            stderr_bytes = container.logs(stdout=False, stderr=True)
            return TestResult(
                passed=exit_code == 0,
                stdout=stdout_bytes.decode("utf-8", errors="replace"),
                stderr=stderr_bytes.decode("utf-8", errors="replace"),
                exit_code=exit_code,
            )
        finally:
            try:
                container.remove(force=True)
            except (APIError, ContainerError):
                pass

    def _create_container(self, repo_path: str, command: list[str]) -> Container:
        return self._client.containers.create(
            image=self._settings.image,
            command=command,
            working_dir="/workspace",
            volumes={repo_path: {"bind": "/workspace", "mode": "rw"}},
            network_disabled=self._settings.network_disabled,
            mem_limit=self._settings.memory_limit,
            detach=True,
            # retries reuse the same host-mounted repo_path across separate containers; without this,
            # a stale .pyc from a failing run can shadow a fix written to disk before the retest.
            environment={"PYTHONDONTWRITEBYTECODE": "1"},
        )
