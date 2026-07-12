#include "cooper/core/orchestrator/master_orchestrator.hpp"

#include "cooper/core/data/sqlite_database.hpp"
#include "cooper/core/embeddings/embedding_provider.hpp"
#include "cooper/core/llm/provider_factory.hpp"
#include "cooper/core/memory/session_cache.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

using cooper::core::data::SqliteDatabase;
using cooper::core::embeddings::EmbeddingProvider;
using cooper::core::llm::CreateProvider;
using cooper::core::llm::ProviderConfig;
using cooper::core::memory::SessionCache;
using cooper::core::orchestrator::MasterOrchestrator;
using cooper::core::orchestrator::MasterOrchestratorConfig;
using cooper::core::orchestrator::MasterOrchestratorResult;

namespace {

// Not exposed on the CLI (the spec's argument list has no --git-author flags); a real product
// entrypoint would source these from the operator's own git identity instead.
constexpr const char* kGitAuthorName = "Cooper Agent";
constexpr const char* kGitAuthorEmail = "cooper-agent@localhost";

std::vector<std::string> SplitOnWhitespace(const std::string& text) {
  std::vector<std::string> parts;
  std::istringstream stream(text);
  std::string part;
  while (stream >> part) {
    parts.push_back(part);
  }
  return parts;
}

std::string ReadFileFully(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("failed to open --brd file: " + path.string());
  }
  std::ostringstream stream;
  stream << file.rdbuf();
  return stream.str();
}

struct ParsedArgs {
  std::filesystem::path repo;
  std::filesystem::path brd;
  ProviderConfig provider_config;
  std::filesystem::path db;
  std::string test_image = "python:3.12-slim";
  std::string test_command = "python -m pytest -q";
  int max_retries = 3;
  std::string orchestrator_dir = COOPER_ORCHESTRATOR_DIR;
};

std::optional<ParsedArgs> ParseArgs(int argc, char** argv) {
  if (argc < 2 || std::string(argv[1]) != "run") {
    return std::nullopt;
  }

  ParsedArgs parsed;
  for (int i = 2; i < argc; ++i) {
    std::string arg = argv[i];
    auto next_value = [&]() -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error("missing value for " + arg);
      }
      return argv[++i];
    };

    if (arg == "--repo") {
      parsed.repo = next_value();
    } else if (arg == "--brd") {
      parsed.brd = next_value();
    } else if (arg == "--provider") {
      parsed.provider_config.provider_name = next_value();
    } else if (arg == "--base-url") {
      parsed.provider_config.base_url = next_value();
    } else if (arg == "--model") {
      parsed.provider_config.model = next_value();
    } else if (arg == "--api-key") {
      parsed.provider_config.api_key = next_value();
    } else if (arg == "--db") {
      parsed.db = next_value();
    } else if (arg == "--test-image") {
      parsed.test_image = next_value();
    } else if (arg == "--test-command") {
      parsed.test_command = next_value();
    } else if (arg == "--max-retries") {
      parsed.max_retries = std::stoi(next_value());
    } else if (arg == "--orchestrator-dir") {
      parsed.orchestrator_dir = next_value();
    } else {
      throw std::runtime_error("unknown argument " + arg);
    }
  }

  if (parsed.repo.empty() || parsed.brd.empty() || parsed.provider_config.provider_name.empty() ||
      parsed.provider_config.base_url.empty() || parsed.provider_config.model.empty()) {
    return std::nullopt;
  }

  // No --db given: park the database next to the repo rather than under /tmp, so a run's
  // history survives a reboot the way a real product entrypoint's state should.
  if (parsed.db.empty()) {
    parsed.db = parsed.repo.parent_path() / (parsed.repo.filename().string() + ".cooper.db");
  }

  return parsed;
}

void PrintUsage() {
  std::cerr << "usage: cooper_master run --repo <path> --brd <file> --provider <p> --base-url <u> "
               "--model <m> [--api-key <k>] [--db <path>] [--test-image <img>] "
               "[--test-command \"<cmd>\"] [--max-retries <n>] [--orchestrator-dir <dir>]\n"
               "  --db defaults to <repo-parent>/<repo-name>.cooper.db\n"
               "  --test-image defaults to python:3.12-slim\n"
               "  --test-command defaults to \"python -m pytest -q\"\n"
               "  --max-retries defaults to 3\n";
}

}  // namespace

int main(int argc, char** argv) {
  std::optional<ParsedArgs> parsed;
  try {
    parsed = ParseArgs(argc, argv);
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }
  if (!parsed) {
    PrintUsage();
    return 1;
  }

  // Same rationale as agent_ping.cpp: this tool's whole purpose is to exercise tool calling.
  parsed->provider_config.assume_tool_calling_supported = true;

  try {
    std::string business_requirement = ReadFileFully(parsed->brd);

    auto provider = CreateProvider(parsed->provider_config);
    auto* embedder = dynamic_cast<EmbeddingProvider*>(provider.get());
    if (embedder == nullptr) {
      throw std::runtime_error("configured provider '" + parsed->provider_config.provider_name +
                                "' does not implement EmbeddingProvider");
    }

    SqliteDatabase db(parsed->db);
    std::filesystem::path cache_path = parsed->db.parent_path() / (parsed->db.stem().string() + ".session_cache.json");
    SessionCache cache = SessionCache::Load(cache_path);

    MasterOrchestratorConfig config;
    config.repo_path = parsed->repo;
    config.business_requirement = business_requirement;
    config.max_retries_per_subtask = parsed->max_retries;
    config.git_author_name = kGitAuthorName;
    config.git_author_email = kGitAuthorEmail;
    config.search_token_budget = 2000;
    config.run_tests_config.test_command = SplitOnWhitespace(parsed->test_command);
    config.run_tests_config.docker_image = parsed->test_image;
    config.run_tests_config.timeout_seconds = 60;
    config.run_tests_config.orchestrator_dir = parsed->orchestrator_dir;

    std::cout << "Repo: " << config.repo_path.string() << "\n";
    std::cout << "Provider: " << provider->Name() << " (model " << parsed->provider_config.model << ")\n";
    std::cout << "Database: " << parsed->db.string() << "\n";
    std::cout << "Max retries per subtask: " << config.max_retries_per_subtask << "\n";
    std::cout << "Running Master Orchestrator (product_manager -> scheduler -> per-subtask "
                 "coder/tests/manager loop)...\n\n";

    MasterOrchestrator orchestrator(*provider, db, cache, *embedder, config);
    MasterOrchestratorResult result = orchestrator.Run();

    std::cout << "\nRun id: " << result.run_id << "\n";
    std::cout << "Completed: " << (result.completed ? "yes" : "no") << "\n";
    std::cout << "Failed: " << (result.failed ? "yes" : "no") << "\n";
    std::cout << "Subtask outcomes:\n";
    for (const auto& outcome : result.subtask_outcomes) {
      std::cout << "  " << outcome.subtask_id << ": " << (outcome.succeeded ? "succeeded" : "failed")
                << " (retries: " << outcome.retry_count << ")\n";
    }
  } catch (const std::exception& error) {
    std::cerr << "error: " << error.what() << "\n";
    return 1;
  }

  return 0;
}
