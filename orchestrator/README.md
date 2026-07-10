# cooper-orchestrator

Phase 2 of Cooper: a Python orchestration layer that runs an agent pipeline
(Product Manager -> Scheduler -> Coder -> Tester) on top of the `cooper_core`
C++ engine built in `core/`. `cooper_core` provides AST-scoped parsing, git
operations, and token-budget-aware context packing; this package wires those
primitives into a LangGraph state machine driven by an LLM (Ollama via
LiteLLM) and a Docker sandbox for running the target repo's test suite.

## Layout

- `cooper_orchestrator/config.py` — `Settings` (pydantic-settings), loaded
  from `config.yaml` (searched in the current working directory, then the
  path in `COOPER_CONFIG_PATH`) with `COOPER_`-prefixed environment variable
  overrides (`COOPER_LLM__MODEL`, etc. — `__` is the nested delimiter).
- `cooper_orchestrator/schemas.py` — Pydantic v2 models that cross agent
  boundaries: `SubTask`, `TechnicalSpec`, `TaskAllocation`, `CoderOutput`,
  `TestResult`, `SuggestedFix`, `TaskState`.
- `cooper_orchestrator/core_bridge.py` — adds `core/build` to `sys.path` and
  wraps `cooper_core.Parser`/`ContextPacker`/`Repository` calls. For
  non-`.py` files it skips the C++ parser (Python-only grammar; feeding it
  another language silently misparses rather than erroring) and falls back
  to truncated raw text.
- `cooper_orchestrator/llm/client.py` — `LLMClient` protocol,
  `LiteLLMClient` (hits Ollama through `litellm.completion`, retries once on
  invalid JSON), `FakeLLMClient` (canned responses for tests).
- `cooper_orchestrator/sandbox/docker_sandbox.py` — `DockerSandbox` runs a
  command against a repo mounted read-write into a container via the
  `docker` Python SDK.
- `cooper_orchestrator/agents/` — one pure function per stage:
  `product_manager.run`, `scheduler.run`, `coder.run`, `tester.run`, each
  `(TaskState, LLMClient, Settings) -> TaskState`.
- `cooper_orchestrator/graph.py` — LangGraph `StateGraph` wiring the four
  agents into a loop bounded by `settings.max_retries`.
- `cooper_orchestrator/cli.py` — `python -m cooper_orchestrator.cli run
  --repo <path> --brd <file>`.

## Setup

```
cmake -S ../core -B ../core/build -DCOOPER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Release
cmake --build ../core/build -j
python3 -m venv .venv && source .venv/bin/activate
pip install -e ".[dev]"
pytest tests -v
```

`tests/test_docker_sandbox.py` and `tests/test_graph_end_to_end.py` run
containers for real against the local Docker daemon; the rest of the suite
uses `FakeLLMClient` and never makes a network call, so the full suite runs
in well under a minute without Ollama installed.

## Docker

```
docker compose build orchestrator
docker compose run --rm orchestrator run --repo <path> --brd <path>
```

The image builds `core/` from source (it needs to match the container's
platform/architecture — a macOS-built `.so` won't load in a Linux
container) and installs `requirements.txt` on top of `ubuntu:24.04`.

Two things this container needs that aren't obvious from the Dockerfile
alone:

- **It mounts `/var/run/docker.sock`.** The Tester agent's `DockerSandbox`
  needs to launch its own containers to run the target repo's tests. Since
  this container doesn't run Docker-in-Docker, it talks to the *host's*
  Docker daemon via the mounted socket — the sandbox containers it spawns
  are siblings of this container, not children. This is standard practice,
  but note it hands this container the same privileges as root on the host;
  don't run it against untrusted input without being aware of that.
- **`--repo` must point inside the shared `workspace/` volume, using the
  same host-absolute path on both sides** (`docker-compose.yml` mounts
  `${PWD}/workspace:${PWD}/workspace`, not e.g. `/workspace`). This is
  required, not cosmetic: because sandbox containers are siblings spawned
  via the host daemon, the host daemon resolves any path this container
  asks it to mount against the *host* filesystem, not this container's own
  filesystem. If the repo path isn't identical on both sides, the sandbox
  container mounts the wrong thing (or nothing). Example:
  `docker compose run --rm orchestrator run --repo $(pwd)/workspace/end-to-end-encryption --brd $(pwd)/workspace/end-to-end-encryption/REQUIREMENT.md`.

By default the LLM points at `http://host.docker.internal:11434` (Ollama
running on the host, reachable from the container via
`extra_hosts: host-gateway`) — override with `COOPER_LLM__BASE_URL` /
`COOPER_LLM__MODEL` env vars if Ollama runs elsewhere.

## Documented scope limitations (this pass only)

- **Full-file replacement, not diffs.** The Coder agent returns the entire
  new content of one target file per subtask (`CoderOutput.new_content`),
  not a unified diff. Diff generation and patch application are a real,
  separate problem; full-file replacement is simpler and enough to prove
  the pipeline end to end.
- **Product Manager has no repo context.** It turns the raw business
  requirement into a `TechnicalSpec` directly from the LLM, without calling
  `core_bridge.get_context_for_file` first. Repo-context-aware spec
  generation needs multi-file retrieval ranking, which doesn't exist yet.
- **Scheduler makes a real LLM call** to order subtasks by dependency,
  falling back to the original order if the model returns an invalid
  ordering (wrong id set). It does not merge, split, or re-scope subtasks —
  only orders the ones Product Manager already produced.
- **Tester suggests a fix on failure.** On a failing test run it calls the
  LLM for a `SuggestedFix` (diagnosis + concrete fix), which the Coder gets
  ahead of the raw stdout/stderr on retry. The pipeline loops
  Coder→Tester→Coder per subtask until it passes or `max_retries` is hit,
  then advances to the next subtask (retry budget resets per subtask) with
  its own commit, until all subtasks are done.
- **Context ranking is trivial.** `core_bridge.get_context_for_file` packs
  chunks in file order, not by relevance ranking.
- **No Manager/review agent.** Deferred entirely.
- **No git branch/push/PR.** `core_bridge.commit_change` stages and commits
  on whatever branch is currently checked out. Creating branches, pushing,
  and opening PRs are explicitly out of scope for this pass.
- **Docker stdout/stderr separation.** `DockerSandbox` requests stdout and
  stderr from the `docker` SDK's `container.logs()` in two separate calls
  (`stdout=True, stderr=False` and vice versa); this relies on the SDK's own
  demultiplexing of the non-tty log stream rather than a raw single fetch.
