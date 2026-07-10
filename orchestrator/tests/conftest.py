from __future__ import annotations

import shutil
import subprocess
import tempfile
from collections.abc import Iterator
from pathlib import Path

import docker
import pytest

from cooper_orchestrator.config import GitSettings, LLMSettings, SandboxSettings, Settings

_FIXTURES_DIR = Path(__file__).parent / "fixtures"
_PYTEST_IMAGE_TAG = "cooper-orchestrator-test-pytest:latest"


@pytest.fixture
def golden_repo_path(tmp_path: Path) -> str:
    repo_dir = tmp_path / "golden_repo"
    shutil.copytree(_FIXTURES_DIR / "golden_repo", repo_dir)
    subprocess.run(["git", "init", "-q"], cwd=repo_dir, check=True)
    return str(repo_dir)


@pytest.fixture
def golden_brd_text() -> str:
    return (_FIXTURES_DIR / "golden_brd.txt").read_text()


@pytest.fixture(scope="session")
def docker_client() -> Iterator[docker.DockerClient]:
    client = docker.from_env()
    yield client
    client.close()


@pytest.fixture(scope="session")
def sandbox_pytest_image(docker_client: docker.DockerClient) -> Iterator[str]:
    with tempfile.TemporaryDirectory() as build_context_dir:
        dockerfile_path = Path(build_context_dir) / "Dockerfile"
        dockerfile_path.write_text("FROM python:3.12-slim\nRUN pip install --no-cache-dir pytest\n")
        docker_client.images.build(path=build_context_dir, tag=_PYTEST_IMAGE_TAG, rm=True)
    yield _PYTEST_IMAGE_TAG


@pytest.fixture
def sandbox_settings(sandbox_pytest_image: str) -> SandboxSettings:
    return SandboxSettings(
        image=sandbox_pytest_image,
        timeout_seconds=30,
        memory_limit="512m",
        network_disabled=True,
    )


@pytest.fixture
def test_settings(sandbox_settings: SandboxSettings, tmp_path: Path) -> Settings:
    return Settings(
        llm=LLMSettings(),
        sandbox=sandbox_settings,
        git=GitSettings(author_name="Cooper Test", author_email="cooper-test@local"),
        max_retries=3,
    )
