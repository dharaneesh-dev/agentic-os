#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include "cooper/core/context/context_packer.hpp"
#include "cooper/core/embeddings/mock_embedding_provider.hpp"
#include "cooper/core/git/repository.hpp"
#include "cooper/core/parser/parser.hpp"
#include "cooper/core/vectorstore/vector_index.hpp"

namespace py = pybind11;

using cooper::core::context::ContextPacker;
using cooper::core::context::PackedContext;
using cooper::core::embeddings::EmbeddingProvider;
using cooper::core::embeddings::MockEmbeddingProvider;
using cooper::core::git::Repository;
using cooper::core::parser::CodeChunk;
using cooper::core::parser::Parser;
using cooper::core::vectorstore::VectorIndex;

PYBIND11_MODULE(cooper_core, m) {
  m.doc() = "Cooper core: parsing, git, vector search, embeddings, and context packing";

  py::class_<CodeChunk>(m, "CodeChunk")
      .def(py::init<>())
      .def_readwrite("kind", &CodeChunk::kind)
      .def_readwrite("name", &CodeChunk::name)
      .def_readwrite("start_line", &CodeChunk::start_line)
      .def_readwrite("end_line", &CodeChunk::end_line)
      .def_readwrite("source_text", &CodeChunk::source_text);

  py::class_<Parser>(m, "Parser")
      .def(py::init<>())
      .def("parse_file", &Parser::ParseFile, py::arg("path"));

  py::class_<Repository>(m, "Repository")
      .def_static("init", &Repository::Init, py::arg("path"))
      .def_static("clone", &Repository::Clone, py::arg("url"), py::arg("into"))
      .def_static("open", &Repository::Open, py::arg("path"))
      .def("create_branch", &Repository::CreateBranch, py::arg("name"))
      .def("checkout_branch", &Repository::CheckoutBranch, py::arg("name"))
      .def("stage_all", &Repository::StageAll)
      .def("commit", &Repository::Commit, py::arg("message"), py::arg("author_name"), py::arg("author_email"))
      .def("current_branch", &Repository::CurrentBranch);

  py::class_<VectorIndex>(m, "VectorIndex")
      .def(py::init<size_t, size_t>(), py::arg("dim"), py::arg("max_elements"))
      .def("add", &VectorIndex::Add, py::arg("id"), py::arg("vector"))
      .def("search", &VectorIndex::Search, py::arg("query"), py::arg("k"));

  py::class_<EmbeddingProvider> embedding_provider(m, "EmbeddingProvider");

  py::class_<MockEmbeddingProvider, EmbeddingProvider>(m, "MockEmbeddingProvider")
      .def(py::init<size_t>(), py::arg("dim") = 64)
      .def("embed", &MockEmbeddingProvider::Embed, py::arg("text"))
      .def("dimension", &MockEmbeddingProvider::Dimension);

  py::class_<PackedContext>(m, "PackedContext")
      .def(py::init<>())
      .def_readwrite("included_chunks", &PackedContext::included_chunks)
      .def_readwrite("total_tokens_estimate", &PackedContext::total_tokens_estimate);

  py::class_<ContextPacker>(m, "ContextPacker")
      .def(py::init<>())
      .def("pack", &ContextPacker::Pack, py::arg("ranked_chunks"), py::arg("token_budget"));
}
