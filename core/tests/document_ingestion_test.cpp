#include "cooper/core/knowledge/document_ingestion.hpp"

#include "cooper/core/data/sqlite_database.hpp"
#include "cooper/core/embeddings/mock_embedding_provider.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <random>

using cooper::core::data::SqliteDatabase;
using cooper::core::embeddings::MockEmbeddingProvider;
using cooper::core::knowledge::ChunkDocument;
using cooper::core::knowledge::DocumentIngestor;
using cooper::core::knowledge::DocumentSection;

namespace {

std::filesystem::path MakeTempDir() {
  std::random_device rd;
  std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("cooper_core_document_ingestion_test_" + std::to_string(rd()));
  std::filesystem::create_directories(dir);
  return dir;
}

void WriteFile(const std::filesystem::path& path, const std::string& content) {
  std::ofstream file(path, std::ios::binary);
  file << content;
}

}  // namespace

TEST(ChunkDocumentTest, SplitsMarkdownOnHeadings) {
  std::filesystem::path dir = MakeTempDir();
  std::filesystem::path path = dir / "notes.md";
  WriteFile(path, "# Intro\nWelcome text.\n\n## Details\nMore text here.\n");

  std::vector<DocumentSection> sections = ChunkDocument(path);

  ASSERT_EQ(sections.size(), 2u);
  EXPECT_EQ(sections[0].heading, "Intro");
  EXPECT_EQ(sections[0].content, "Welcome text.");
  EXPECT_EQ(sections[1].heading, "Details");
  EXPECT_EQ(sections[1].content, "More text here.");

  std::filesystem::remove_all(dir);
}

TEST(ChunkDocumentTest, SplitsPlainTextOnBlankLines) {
  std::filesystem::path dir = MakeTempDir();
  std::filesystem::path path = dir / "notes.txt";
  WriteFile(path, "First paragraph.\n\nSecond paragraph.\n\n\nThird paragraph.\n");

  std::vector<DocumentSection> sections = ChunkDocument(path);

  ASSERT_EQ(sections.size(), 3u);
  EXPECT_EQ(sections[0].content, "First paragraph.");
  EXPECT_EQ(sections[1].content, "Second paragraph.");
  EXPECT_EQ(sections[2].content, "Third paragraph.");

  std::filesystem::remove_all(dir);
}

TEST(ChunkDocumentTest, ThrowsOnUnsupportedExtension) {
  EXPECT_THROW(ChunkDocument("/nonexistent/path/does_not_exist.pdf"), std::runtime_error);
}

TEST(DocumentIngestorTest, IngestsMarkdownDocumentIntoDatabase) {
  std::filesystem::path dir = MakeTempDir();
  std::filesystem::path db_path = dir / "test.db";
  std::filesystem::path doc_path = dir / "README.md";
  WriteFile(doc_path, "# Overview\nThis project does things.\n\n# Usage\nRun it like this.\n");

  SqliteDatabase db(db_path);
  MockEmbeddingProvider embedder(16);
  DocumentIngestor ingestor(db, embedder);

  cooper::core::data::Codebase codebase;
  codebase.created_at = "2026-07-10T00:00:00Z";
  int64_t codebase_id = db.CreateCodebase(codebase);

  int64_t document_id = ingestor.Ingest(codebase_id, doc_path);
  EXPECT_GT(document_id, 0);

  auto chunks = db.GetChunksForDocument(document_id);
  ASSERT_EQ(chunks.size(), 2u);
  EXPECT_NE(chunks[0].content.find("Overview"), std::string::npos);
  EXPECT_NE(chunks[1].content.find("Usage"), std::string::npos);
  EXPECT_FALSE(chunks[0].embedding_json.empty());

  std::filesystem::remove_all(dir);
}
