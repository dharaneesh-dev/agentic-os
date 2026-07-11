#include "cooper/core/llm/provider_factory.hpp"

#include "cooper/core/llm/anthropic_provider.hpp"
#include "cooper/core/llm/gemini_provider.hpp"
#include "cooper/core/llm/ollama_provider.hpp"
#include "cooper/core/llm/openai_provider.hpp"

#include <gtest/gtest.h>

using cooper::core::llm::AnthropicProvider;
using cooper::core::llm::CreateProvider;
using cooper::core::llm::GeminiProvider;
using cooper::core::llm::OllamaProvider;
using cooper::core::llm::OpenAiProvider;
using cooper::core::llm::ProviderConfig;

TEST(ProviderFactoryTest, CreatesOllamaProvider) {
  ProviderConfig config;
  config.provider_name = "ollama";
  auto provider = CreateProvider(config);
  EXPECT_EQ(provider->Name(), "ollama");
  EXPECT_NE(dynamic_cast<OllamaProvider*>(provider.get()), nullptr);
}

TEST(ProviderFactoryTest, CreatesOpenAiProvider) {
  ProviderConfig config;
  config.provider_name = "openai";
  auto provider = CreateProvider(config);
  EXPECT_EQ(provider->Name(), "openai");
  EXPECT_NE(dynamic_cast<OpenAiProvider*>(provider.get()), nullptr);
}

TEST(ProviderFactoryTest, CreatesAnthropicProvider) {
  ProviderConfig config;
  config.provider_name = "anthropic";
  auto provider = CreateProvider(config);
  EXPECT_EQ(provider->Name(), "anthropic");
  EXPECT_NE(dynamic_cast<AnthropicProvider*>(provider.get()), nullptr);
}

TEST(ProviderFactoryTest, CreatesGeminiProvider) {
  ProviderConfig config;
  config.provider_name = "gemini";
  auto provider = CreateProvider(config);
  EXPECT_EQ(provider->Name(), "gemini");
  EXPECT_NE(dynamic_cast<GeminiProvider*>(provider.get()), nullptr);
}

TEST(ProviderFactoryTest, ThrowsOnUnknownProviderName) {
  ProviderConfig config;
  config.provider_name = "not-a-real-provider";
  EXPECT_THROW(CreateProvider(config), std::runtime_error);
}
