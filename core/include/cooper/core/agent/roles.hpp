#pragma once

#include "cooper/core/agent/agent_loop.hpp"
#include "cooper/core/agent/tool.hpp"
#include "cooper/core/embeddings/embedding_provider.hpp"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace cooper::core::agent {

struct RoleSetup {
  AgentLoopConfig config;
  std::vector<std::unique_ptr<Tool>> tools;
};

struct RunTestsConfig {
  std::vector<std::string> test_command;
  std::string docker_image;
  int timeout_seconds;
  std::filesystem::path orchestrator_dir;
  std::string python_executable = "python3";
};

RoleSetup MakeCoderRole(const std::filesystem::path& repo_root, embeddings::EmbeddingProvider& embedder,
                         int search_token_budget, const RunTestsConfig& run_tests_config);

RoleSetup MakeProductManagerRole(const std::filesystem::path& repo_root, embeddings::EmbeddingProvider& embedder,
                                  int search_token_budget);

RoleSetup MakeManagerRole(const std::filesystem::path& repo_root);

}  // namespace cooper::core::agent
