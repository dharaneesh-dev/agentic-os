#pragma once

// Shared, provider-internal HTTP/JSON plumbing for the llm module. Not a public header -- lives
// alongside the .cpp files that use it, matching this module's split between the public interface
// in include/cooper/core/llm and the provider-specific request/response shaping in src/llm.

#include <httplib.h>
#include <nlohmann/json.hpp>

#include <memory>
#include <stdexcept>
#include <string>

namespace cooper::core::llm::detail {

inline std::unique_ptr<httplib::Client> MakeClient(const std::string& base_url, int timeout_seconds) {
  auto client = std::make_unique<httplib::Client>(base_url);
  client->set_connection_timeout(timeout_seconds, 0);
  client->set_read_timeout(timeout_seconds, 0);
  client->set_write_timeout(timeout_seconds, 0);
  return client;
}

inline nlohmann::json PostJson(httplib::Client& client, const std::string& path, const httplib::Headers& headers,
                                const nlohmann::json& body, const std::string& context) {
  auto res = client.Post(path, headers, body.dump(), "application/json");
  if (!res) {
    throw std::runtime_error(context + ": HTTP request failed: " + httplib::to_string(res.error()));
  }
  if (res->status < 200 || res->status >= 300) {
    throw std::runtime_error(context + ": HTTP " + std::to_string(res->status) + ": " + res->body);
  }
  return nlohmann::json::parse(res->body);
}

inline nlohmann::json ParseJsonSchemaOrEmptyObject(const std::string& schema_text) {
  if (schema_text.empty()) {
    return nlohmann::json::object();
  }
  return nlohmann::json::parse(schema_text);
}

}  // namespace cooper::core::llm::detail
