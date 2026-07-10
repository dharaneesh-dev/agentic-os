# cooper-core

C++20 ingestion/context engine for Cooper. Parses source files into AST-scoped
chunks (tree-sitter), tracks git state (libgit2), indexes embeddings for
semantic retrieval (hnswlib), and packs a token-bounded context bundle for
coding agents. Exposed to Python as an in-process pybind11 extension module,
`cooper_core`.

## Modules

- `parser` — wraps tree-sitter + tree-sitter-python; splits a Python file into
  function/class `CodeChunk`s.
- `git` — wraps libgit2; init/clone/open a repo, branch, stage, commit.
- `vectorstore` — wraps hnswlib; a minimal `VectorIndex` for nearest-neighbor
  search over embeddings.
- `embeddings` — `EmbeddingProvider` interface, a deterministic
  `MockEmbeddingProvider` for tests/dev, and a stubbed `LlamaEmbeddingProvider`.
- `context` — `ContextPacker` greedily packs ranked chunks into a
  token-bounded `PackedContext`.
- `bindings` — pybind11 module (`cooper_core`) exposing all of the above to
  Python.

## Build

```
cmake -S core -B core/build -DCOOPER_BUILD_TESTS=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build core/build -j
ctest --test-dir core/build --output-on-failure
```

Dependencies (tree-sitter, tree-sitter-python, hnswlib, pybind11,
GoogleTest) are pulled via CMake `FetchContent` on first configure. libgit2
is expected to be available on the system; it is located with
`find_package(libgit2 CONFIG)` and falls back to `pkg-config`.

### Platform prerequisites

| Platform | Install libgit2 + pkg-config |
|---|---|
| macOS | `brew install libgit2 pkg-config` |
| Linux (Debian/Ubuntu) | `sudo apt-get install libgit2-dev pkg-config` |
| Linux (Fedora) | `sudo dnf install libgit2-devel pkgconf-pkg-config` |
| Windows | via [vcpkg](https://github.com/microsoft/vcpkg): `vcpkg install libgit2:x64-windows`, then configure with `-DCMAKE_TOOLCHAIN_FILE=<vcpkg>/scripts/buildsystems/vcpkg.cmake` |

All three platforms are exercised on every push via
`.github/workflows/core-ci.yml` (ubuntu-latest / windows-latest /
macos-latest matrix) — that CI run is the actual cross-platform guarantee;
day-to-day development on this machine only verifies macOS.

### Docker (local Linux verification)

```
docker compose build cooper-core
docker compose up cooper-core
```

Builds and runs the full test suite inside `ubuntu:24.04`, matching the
`ubuntu-latest` leg of CI — useful for checking Linux behavior from a
non-Linux dev machine without waiting on a CI run.

## CMake options

- `COOPER_BUILD_TESTS` (default `ON`) — build the `cooper_core_tests` suite.
- `COOPER_BUILD_PYTHON_BINDINGS` (default `ON`) — build the `cooper_core`
  pybind11 extension module.
- `COOPER_ENABLE_LLAMA_CPP` (default `OFF`) — compile
  `LlamaEmbeddingProvider`. Currently every method of that provider throws;
  llama.cpp is not yet wired up as a dependency.

## Scope boundary

`LlamaEmbeddingProvider` is a stub only — no llama.cpp dependency is fetched
or built. Real embedding generation is out of scope for this phase;
`MockEmbeddingProvider` stands in as a deterministic placeholder.
