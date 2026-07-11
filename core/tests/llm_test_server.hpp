#pragma once

// Shared real-loopback-HTTP test helper for the llm provider tests: spins up an httplib::Server
// on an OS-assigned port so each provider test can point a provider's base_url at 127.0.0.1 and
// assert on the exact request the provider sent, rather than mocking the provider's own methods.

#include <httplib.h>

#include <atomic>
#include <chrono>
#include <string>
#include <thread>

namespace cooper::core::llm::testing {

class MockServer {
 public:
  MockServer() {
    port_ = server_.bind_to_any_port("127.0.0.1");
    thread_ = std::thread([this]() { server_.listen_after_bind(); });
    while (!server_.is_running()) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  }

  ~MockServer() {
    server_.stop();
    if (thread_.joinable()) {
      thread_.join();
    }
  }

  MockServer(const MockServer&) = delete;
  MockServer& operator=(const MockServer&) = delete;

  httplib::Server& server() { return server_; }
  std::string base_url() const { return "http://127.0.0.1:" + std::to_string(port_); }

  void RecordRequest() { ++request_count_; }
  int request_count() const { return request_count_.load(); }

 private:
  httplib::Server server_;
  std::thread thread_;
  int port_ = 0;
  std::atomic<int> request_count_{0};
};

}  // namespace cooper::core::llm::testing
