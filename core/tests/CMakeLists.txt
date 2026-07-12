include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

cooper_no_rewrite_git_config(COOPER_GTEST_GIT_CONFIG "https://github.com/google/googletest.git")

FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG v1.15.2
  GIT_CONFIG "${COOPER_GTEST_GIT_CONFIG}"
)
FetchContent_MakeAvailable(googletest)

add_executable(cooper_core_tests
  parser_test.cpp
  git_repository_test.cpp
  vector_index_test.cpp
  context_packer_test.cpp
  mock_embedding_provider_test.cpp
  sqlite_database_test.cpp
  session_cache_test.cpp
  usage_ledger_test.cpp
  ollama_provider_test.cpp
  openai_provider_test.cpp
  anthropic_provider_test.cpp
  gemini_provider_test.cpp
  provider_factory_test.cpp
  read_file_tool_test.cpp
  write_file_tool_test.cpp
  search_codebase_tool_test.cpp
  run_tests_tool_test.cpp
  agent_loop_test.cpp
  master_orchestrator_test.cpp
)

target_link_libraries(cooper_core_tests PRIVATE cooper_core GTest::gtest_main GTest::gtest)

target_compile_definitions(cooper_core_tests PRIVATE
  COOPER_TEST_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
  COOPER_ORCHESTRATOR_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../orchestrator"
)

include(GoogleTest)
gtest_discover_tests(cooper_core_tests)
