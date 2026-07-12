#include "cooper/core/knowledge/document_ingestion.hpp"

#include <nlohmann/json.hpp>

#include <fstream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace cooper::core::knowledge {

namespace {

std::string ReadFile(const std::filesystem::path& path) {
  std::ifstream file(path, std::ios::binary);
  if (!file) {
    throw std::runtime_error("ChunkDocument: unable to read file " + path.string());
  }
  std::ostringstream contents;
  contents << file.rdbuf();
  return contents.str();
}

std::string Trim(const std::string& text) {
  size_t start = text.find_first_not_of(" \t\r\n");
  if (start == std::string::npos) {
    return "";
  }
  size_t end = text.find_last_not_of(" \t\r\n");
  return text.substr(start, end - start + 1);
}

std::vector<DocumentSection> ChunkMarkdown(const std::string& source) {
  static const std::regex kHeadingRegex(R"(^#+\s+(.*)$)");

  std::vector<DocumentSection> sections;
  std::istringstream stream(source);
  std::string line;
  std::string heading;
  std::ostringstream content;
  bool have_section = false;

  auto Flush = [&]() {
    std::string trimmed = Trim(content.str());
    if (have_section || !trimmed.empty()) {
      sections.push_back(DocumentSection{heading, trimmed});
    }
    content.str("");
    content.clear();
  };

  while (std::getline(stream, line)) {
    std::smatch match;
    if (std::regex_match(line, match, kHeadingRegex)) {
      Flush();
      heading = Trim(match[1].str());
      have_section = true;
    } else {
      content << line << "\n";
    }
  }
  Flush();
  return sections;
}

std::vector<DocumentSection> ChunkPlainText(const std::string& source) {
  static const std::regex kParagraphSplit(R"(\n[ \t\r]*\n)");

  std::vector<DocumentSection> sections;
  std::sregex_token_iterator it(source.begin(), source.end(), kParagraphSplit, -1);
  std::sregex_token_iterator end;
  for (; it != end; ++it) {
    std::string paragraph = Trim(*it);
    if (!paragraph.empty()) {
      sections.push_back(DocumentSection{"", paragraph});
    }
  }
  return sections;
}

}  // namespace

std::vector<DocumentSection> ChunkDocument(const std::filesystem::path& path) {
  std::string extension = path.extension().string();
  std::string source = ReadFile(path);
  if (extension == ".md") {
    return ChunkMarkdown(source);
  }
  if (extension == ".txt") {
    return ChunkPlainText(source);
  }
  throw std::runtime_error("ChunkDocument: unsupported file extension for " + path.string() +
                            "; only .md and .txt are currently supported");
}

DocumentIngestor::DocumentIngestor(data::IDatabase& db, embeddings::EmbeddingProvider& embedder)
    : db_(db), embedder_(embedder) {}

int64_t DocumentIngestor::Ingest(int64_t codebase_id, const std::filesystem::path& path) {
  data::KnowledgeDocument document;
  document.codebase_id = codebase_id;
  document.source = path.string();
  document.title = path.filename().string();
  int64_t document_id = db_.CreateKnowledgeDocument(document);

  for (const auto& section : ChunkDocument(path)) {
    std::string text =
        section.heading.empty() ? section.content : ("# " + section.heading + "\n" + section.content);

    data::KnowledgeChunk chunk;
    chunk.document_id = document_id;
    chunk.content = text;
    chunk.embedding_json = nlohmann::json(embedder_.Embed(text)).dump();
    db_.CreateKnowledgeChunk(chunk);
  }

  return document_id;
}

}  // namespace cooper::core::knowledge
