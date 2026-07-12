cmake_minimum_required(VERSION 3.21)
project(cooper_core CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

option(COOPER_BUILD_TESTS "Build cooper_core unit tests" ON)
option(COOPER_ENABLE_LLAMA_CPP "Enable llama.cpp-backed embedding provider" OFF)
option(COOPER_BUILD_PYTHON_BINDINGS "Build pybind11 Python bindings" ON)
option(COOPER_BUILD_TOOLS "Build cooper_core CLI tools" ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(FetchContent)

find_package(PkgConfig QUIET)

find_package(libgit2 CONFIG QUIET)
if(TARGET libgit2::libgit2)
  set(COOPER_LIBGIT2_TARGET libgit2::libgit2)
elseif(TARGET libgit2::libgit2package)
  set(COOPER_LIBGIT2_TARGET libgit2::libgit2package)
elseif(libgit2_FOUND)
  set(COOPER_LIBGIT2_TARGET libgit2)
else()
  find_package(PkgConfig REQUIRED)
  pkg_check_modules(LIBGIT2 REQUIRED libgit2)
endif()

# OpenSSL backs cpp-httplib's HTTPS support (needed by the OpenAI/Anthropic/Gemini LLM providers;
# Ollama is typically plain HTTP on localhost). Do NOT silently fall back to no-TLS -- fail loudly
# with install instructions instead, since 3 of 4 providers would otherwise be silently broken.
find_package(OpenSSL)
if(NOT OpenSSL_FOUND)
  message(FATAL_ERROR
    "OpenSSL not found. cooper_core requires OpenSSL for HTTPS support in the LLM provider "
    "clients (OpenAI/Anthropic/Gemini are HTTPS-only). Install it and re-run CMake:\n"
    "  macOS:   brew install openssl\n"
    "  Linux:   sudo apt-get install -y libssl-dev\n"
    "  Windows: vcpkg install openssl:x64-windows (with -DCMAKE_TOOLCHAIN_FILE pointing at vcpkg)")
endif()

function(cooper_no_rewrite_git_config out_var url)
  set(${out_var} "url.${url}.insteadOf=${url}" PARENT_SCOPE)
endfunction()

cooper_no_rewrite_git_config(COOPER_TS_GIT_CONFIG "https://github.com/tree-sitter/tree-sitter.git")

FetchContent_Declare(
  tree_sitter
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG v0.24.6
  GIT_CONFIG "${COOPER_TS_GIT_CONFIG}"
)
FetchContent_GetProperties(tree_sitter)
if(NOT tree_sitter_POPULATED)
  FetchContent_Populate(tree_sitter)
endif()

add_library(tree_sitter STATIC
  ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
)
target_include_directories(tree_sitter PUBLIC
  ${tree_sitter_SOURCE_DIR}/lib/include
)
target_include_directories(tree_sitter PRIVATE
  ${tree_sitter_SOURCE_DIR}/lib/src
)
set_target_properties(tree_sitter PROPERTIES C_STANDARD 11)

cooper_no_rewrite_git_config(COOPER_TSPY_GIT_CONFIG "https://github.com/tree-sitter/tree-sitter-python.git")

FetchContent_Declare(
  tree_sitter_python
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-python.git
  GIT_TAG v0.23.6
  GIT_CONFIG "${COOPER_TSPY_GIT_CONFIG}"
)
FetchContent_GetProperties(tree_sitter_python)
if(NOT tree_sitter_python_POPULATED)
  FetchContent_Populate(tree_sitter_python)
endif()

add_library(tree_sitter_python STATIC
  ${tree_sitter_python_SOURCE_DIR}/src/parser.c
  ${tree_sitter_python_SOURCE_DIR}/src/scanner.c
)
target_include_directories(tree_sitter_python PUBLIC
  ${tree_sitter_python_SOURCE_DIR}/bindings/c
)
target_include_directories(tree_sitter_python PRIVATE
  ${tree_sitter_python_SOURCE_DIR}/src
)
target_link_libraries(tree_sitter_python PUBLIC tree_sitter)
set_target_properties(tree_sitter_python PROPERTIES C_STANDARD 11)

cooper_no_rewrite_git_config(COOPER_TSCPP_GIT_CONFIG "https://github.com/tree-sitter/tree-sitter-cpp.git")

FetchContent_Declare(
  tree_sitter_cpp
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
  GIT_TAG v0.23.4
  GIT_CONFIG "${COOPER_TSCPP_GIT_CONFIG}"
)
FetchContent_GetProperties(tree_sitter_cpp)
if(NOT tree_sitter_cpp_POPULATED)
  FetchContent_Populate(tree_sitter_cpp)
endif()

add_library(tree_sitter_cpp STATIC
  ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
  ${tree_sitter_cpp_SOURCE_DIR}/src/scanner.c
)
target_include_directories(tree_sitter_cpp PUBLIC
  ${tree_sitter_cpp_SOURCE_DIR}/bindings/c
)
target_include_directories(tree_sitter_cpp PRIVATE
  ${tree_sitter_cpp_SOURCE_DIR}/src
)
target_link_libraries(tree_sitter_cpp PUBLIC tree_sitter)
set_target_properties(tree_sitter_cpp PROPERTIES C_STANDARD 11)

cooper_no_rewrite_git_config(COOPER_HNSW_GIT_CONFIG "https://github.com/nmslib/hnswlib.git")

FetchContent_Declare(
  hnswlib
  GIT_REPOSITORY https://github.com/nmslib/hnswlib.git
  GIT_TAG v0.8.0
  GIT_CONFIG "${COOPER_HNSW_GIT_CONFIG}"
)
FetchContent_MakeAvailable(hnswlib)

# SQLite amalgamation: a plain URL download, not a git fetch, so the
# no-rewrite git config override does not apply here.
FetchContent_Declare(
  sqlite_amalgamation
  URL https://sqlite.org/2026/sqlite-amalgamation-3530300.zip
  URL_HASH SHA3_256=d45c688a8cb23f68611a894a756a12d7eb6ab6e9e2468ca70adbeab3808b5ab9
)
FetchContent_GetProperties(sqlite_amalgamation)
if(NOT sqlite_amalgamation_POPULATED)
  FetchContent_Populate(sqlite_amalgamation)
endif()

add_library(sqlite3 STATIC
  ${sqlite_amalgamation_SOURCE_DIR}/sqlite3.c
)
target_include_directories(sqlite3 PUBLIC ${sqlite_amalgamation_SOURCE_DIR})
set_target_properties(sqlite3 PROPERTIES C_STANDARD 11)

cooper_no_rewrite_git_config(COOPER_JSON_GIT_CONFIG "https://github.com/nlohmann/json.git")

set(JSON_BuildTests OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG v3.11.3
  GIT_CONFIG "${COOPER_JSON_GIT_CONFIG}"
)
FetchContent_MakeAvailable(json)

cooper_no_rewrite_git_config(COOPER_HTTPLIB_GIT_CONFIG "https://github.com/yhirose/cpp-httplib.git")

set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)

# Compression support pulls in cpp-httplib's own find_package(ZLIB)/find_package(zstd)/
# find_package(Brotli) probing, none of which this module needs (only plain JSON bodies over
# HTTP/HTTPS). On a machine with Homebrew-installed packages sharing the common
# /opt/homebrew/include prefix, that probing can inject that shared directory into the include
# path ahead of FetchContent-vendored headers (e.g. googletest's), shadowing them with whatever
# happens to also be installed system-wide under the same name. Disabling these avoids that
# entirely, and we don't need response (de)compression for this use case.
set(HTTPLIB_USE_ZLIB_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_BROTLI_IF_AVAILABLE OFF CACHE BOOL "" FORCE)
set(HTTPLIB_USE_ZSTD_IF_AVAILABLE OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
  httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG v0.49.0
  GIT_CONFIG "${COOPER_HTTPLIB_GIT_CONFIG}"
)
FetchContent_MakeAvailable(httplib)

add_library(cooper_core
  src/parser/parser.cpp
  src/git/repository.cpp
  src/vectorstore/vector_index.cpp
  src/embeddings/mock_embedding_provider.cpp
  src/context/context_packer.cpp
  src/data/sqlite_database.cpp
  src/memory/session_cache.cpp
  src/memory/usage_ledger.cpp
  src/llm/provider_factory.cpp
  src/llm/ollama_provider.cpp
  src/llm/openai_provider.cpp
  src/llm/anthropic_provider.cpp
  src/llm/gemini_provider.cpp
  src/agent/read_file_tool.cpp
  src/agent/write_file_tool.cpp
  src/agent/search_codebase_tool.cpp
  src/agent/run_tests_tool.cpp
  src/agent/agent_loop.cpp
  src/agent/roles.cpp
  src/orchestrator/tracking_provider.cpp
  src/orchestrator/master_orchestrator.cpp
  src/knowledge/dependency_graph.cpp
  src/knowledge/document_ingestion.cpp
)

set_target_properties(cooper_core PROPERTIES OUTPUT_NAME cooper_core_static)

if(COOPER_ENABLE_LLAMA_CPP)
  target_sources(cooper_core PRIVATE src/embeddings/llama_embedding_provider.cpp)
  target_compile_definitions(cooper_core PUBLIC COOPER_ENABLE_LLAMA_CPP=1)
endif()

target_include_directories(cooper_core PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(cooper_core PUBLIC
  tree_sitter
  tree_sitter_python
  tree_sitter_cpp
  sqlite3
  nlohmann_json::nlohmann_json
  httplib::httplib
  OpenSSL::SSL
  OpenSSL::Crypto
)

target_compile_definitions(cooper_core PUBLIC CPPHTTPLIB_OPENSSL_SUPPORT)

target_include_directories(cooper_core PRIVATE
  ${hnswlib_SOURCE_DIR}
)

if(COOPER_LIBGIT2_TARGET)
  target_link_libraries(cooper_core PUBLIC ${COOPER_LIBGIT2_TARGET})
else()
  target_include_directories(cooper_core PUBLIC ${LIBGIT2_INCLUDE_DIRS})
  target_link_libraries(cooper_core PUBLIC ${LIBGIT2_LIBRARIES})
  target_link_directories(cooper_core PUBLIC ${LIBGIT2_LIBRARY_DIRS})
endif()

target_compile_features(cooper_core PUBLIC cxx_std_20)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 9.0)
  target_link_libraries(cooper_core PUBLIC stdc++fs)
endif()

if(MSVC)
  target_compile_definitions(cooper_core PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN)
  target_compile_options(cooper_core PUBLIC /utf-8)
endif()

if(COOPER_BUILD_PYTHON_BINDINGS)
  find_package(Python3 COMPONENTS Interpreter Development REQUIRED)

  cooper_no_rewrite_git_config(COOPER_PYBIND11_GIT_CONFIG "https://github.com/pybind/pybind11.git")

  FetchContent_Declare(
    pybind11
    GIT_REPOSITORY https://github.com/pybind/pybind11.git
    GIT_TAG v2.13.6
    GIT_CONFIG "${COOPER_PYBIND11_GIT_CONFIG}"
  )
  FetchContent_MakeAvailable(pybind11)

  pybind11_add_module(cooper_core_py src/bindings/module.cpp)
  target_link_libraries(cooper_core_py PRIVATE cooper_core)
  set_target_properties(cooper_core_py PROPERTIES OUTPUT_NAME cooper_core)
endif()

if(COOPER_BUILD_TOOLS)
  add_executable(cooper_db_tool tools/db_tool.cpp)
  target_link_libraries(cooper_db_tool PRIVATE cooper_core)

  add_executable(cooper_llm_ping tools/llm_ping.cpp)
  target_link_libraries(cooper_llm_ping PRIVATE cooper_core)

  add_executable(cooper_tool_ping tools/tool_ping.cpp)
  target_link_libraries(cooper_tool_ping PRIVATE cooper_core)

  add_executable(cooper_agent_ping tools/agent_ping.cpp)
  target_link_libraries(cooper_agent_ping PRIVATE cooper_core)
  target_compile_definitions(cooper_agent_ping PRIVATE
    COOPER_ORCHESTRATOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../orchestrator")

  add_executable(cooper_master tools/master.cpp)
  target_link_libraries(cooper_master PRIVATE cooper_core)
  target_compile_definitions(cooper_master PRIVATE
    COOPER_ORCHESTRATOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../orchestrator")
endif()

if(COOPER_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
