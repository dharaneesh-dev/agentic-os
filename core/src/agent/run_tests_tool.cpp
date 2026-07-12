#include "cooper/core/agent/run_tests_tool.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <random>
#include <sstream>
#include <stdexcept>

namespace cooper::core::agent {

namespace {

class ScopedEnvVar {
 public:
  ScopedEnvVar(std::string name, const std::string& value) : name_(std::move(name)) {
    const char* previous = std::getenv(name_.c_str());
    if (previous != nullptr) {
      had_previous_ = true;
      previous_value_ = previous;
    }
    Set(name_, value);
  }

  ~ScopedEnvVar() {
    if (had_previous_) {
      Set(name_, previous_value_);
    } else {
      Unset(name_);
    }
  }

  ScopedEnvVar(const ScopedEnvVar&) = delete;
  ScopedEnvVar& operator=(const ScopedEnvVar&) = delete;

 private:
  static void Set(const std::string& name, const std::string& value) {
#ifdef _WIN32
    _putenv_s(name.c_str(), value.c_str());
#else
    setenv(name.c_str(), value.c_str(), 1);
#endif
  }

  static void Unset(const std::string& name) {
#ifdef _WIN32
    _putenv_s(name.c_str(), "");
#else
    unsetenv(name.c_str());
#endif
  }

  std::string name_;
  bool had_previous_ = false;
  std::string previous_value_;
};

// Single-quoting is POSIX shell syntax; Windows's cmd.exe (which _popen invokes) uses
// double-quotes instead, hence the split here rather than one shared implementation.
std::string QuoteArg(const std::string& value) {
#ifdef _WIN32
  return "\"" + value + "\"";
#else
  std::string quoted = "'";
  for (char c : value) {
    quoted += (c == '\'') ? "'\\''" : std::string(1, c);
  }
  quoted += "'";
  return quoted;
#endif
}

nlohmann::json ParseJsonFromOutput(const std::string& output) {
  std::istringstream stream(output);
  std::string line;
  std::vector<std::string> lines;
  while (std::getline(stream, line)) {
    lines.push_back(line);
  }

  for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
    if (it->empty()) {
      continue;
    }
    try {
      return nlohmann::json::parse(*it);
    } catch (const nlohmann::json::parse_error&) {
      continue;
    }
  }

  throw std::runtime_error("run_tests: run_tests_cli produced no parseable JSON output:\n" + output);
}

RunTestsResult ToRunTestsResult(const nlohmann::json& result_json) {
  RunTestsResult result;
  result.passed = result_json.at("passed").get<bool>();
  result.test_stdout = result_json.at("stdout").get<std::string>();
  result.test_stderr = result_json.at("stderr").get<std::string>();
  result.exit_code = result_json.at("exit_code").get<int>();
  return result;
}

std::string FormatResult(const RunTestsResult& result) {
  return std::string(result.passed ? "PASSED" : "FAILED") + " (exit code " + std::to_string(result.exit_code) +
         ")\n\nstdout:\n" + result.test_stdout + "\n\nstderr:\n" + result.test_stderr;
}

}  // namespace

RunTestsTool::RunTestsTool(std::filesystem::path repo_root, std::vector<std::string> test_command,
                            std::string docker_image, int timeout_seconds, std::filesystem::path orchestrator_dir,
                            std::string python_executable)
    : repo_root_(std::move(repo_root)),
      test_command_(std::move(test_command)),
      docker_image_(std::move(docker_image)),
      timeout_seconds_(timeout_seconds),
      orchestrator_dir_(std::move(orchestrator_dir)),
      python_executable_(std::move(python_executable)) {}

llm::ToolDefinition RunTestsTool::Definition() const {
  llm::ToolDefinition tool;
  tool.name = "run_tests";
  tool.description = "Runs the project's configured test command in an isolated sandbox and reports the result.";
  tool.parameters_json_schema = R"({"type":"object","properties":{}})";
  return tool;
}

RunTestsResult RunTestsTool::RunOnce() {
  nlohmann::json payload = {{"repo_path", repo_root_.string()},
                             {"command", test_command_},
                             {"sandbox_settings",
                              {{"image", docker_image_},
                               {"timeout_seconds", timeout_seconds_},
                               {"memory_limit", "512m"},
                               {"network_disabled", true}}}};

  std::random_device rd;
  std::filesystem::path temp_json_path =
      std::filesystem::temp_directory_path() / ("cooper_run_tests_" + std::to_string(rd()) + ".json");

  {
    std::ofstream payload_file(temp_json_path, std::ios::binary);
    if (!payload_file) {
      throw std::runtime_error("run_tests: failed to create temp payload file at " + temp_json_path.string());
    }
    payload_file << payload.dump();
  }

  struct TempFileGuard {
    std::filesystem::path path;
    ~TempFileGuard() {
      std::error_code ec;
      std::filesystem::remove(path, ec);
    }
  } temp_file_guard{temp_json_path};

  ScopedEnvVar pythonpath_guard("PYTHONPATH", orchestrator_dir_.string());

  std::string command = QuoteArg(python_executable_) + " -m cooper_orchestrator.run_tests_cli " +
                         QuoteArg(temp_json_path.string()) + " 2>&1";

#ifdef _WIN32
  FILE* pipe = _popen(command.c_str(), "r");
#else
  FILE* pipe = popen(command.c_str(), "r");
#endif
  if (pipe == nullptr) {
    throw std::runtime_error("run_tests: failed to launch run_tests_cli subprocess");
  }

  std::string raw_output;
  char buffer[4096];
  while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
    raw_output += buffer;
  }

#ifdef _WIN32
  _pclose(pipe);
#else
  pclose(pipe);
#endif

  return ToRunTestsResult(ParseJsonFromOutput(raw_output));
}

std::string RunTestsTool::Execute(const std::string& /*arguments_json*/) { return FormatResult(RunOnce()); }

}  // namespace cooper::core::agent
