#include "cooper/core/knowledge/dependency_graph.hpp"

#include <gtest/gtest.h>

#include <algorithm>

using cooper::core::knowledge::DependencyGraph;
using cooper::core::parser::CodeChunk;

namespace {

bool Contains(const std::vector<std::string>& values, const std::string& value) {
  return std::find(values.begin(), values.end(), value) != values.end();
}

CodeChunk MakeChunk(const std::string& kind, const std::string& name, const std::string& source_text) {
  CodeChunk chunk;
  chunk.kind = kind;
  chunk.name = name;
  chunk.start_line = 1;
  chunk.end_line = 1;
  chunk.source_text = source_text;
  return chunk;
}

}  // namespace

TEST(DependencyGraphTest, FindsCallEdgeBetweenFunctions) {
  DependencyGraph graph;
  graph.AddChunk("checkout.py::checkout", "checkout.py",
                 MakeChunk("function", "checkout", "def checkout(cart):\n    calculate_total(cart.items)\n"));
  graph.AddChunk("orders.py::calculate_total", "orders.py",
                 MakeChunk("function", "calculate_total", "def calculate_total(items):\n    return sum(items)\n"));
  graph.Build();

  EXPECT_TRUE(Contains(graph.DependenciesOf("checkout.py::checkout"), "orders.py::calculate_total"));
  EXPECT_TRUE(Contains(graph.DependentsOf("orders.py::calculate_total"), "checkout.py::checkout"));
}

TEST(DependencyGraphTest, FindsImportEdgeBetweenFiles) {
  DependencyGraph graph;
  graph.AddChunk("main.py::run", "main.py",
                 MakeChunk("function", "run", "def run():\n    import orders\n    orders.calculate_total([])\n"));
  graph.AddChunk("orders.py::calculate_total", "orders.py",
                 MakeChunk("function", "calculate_total", "def calculate_total(items):\n    return sum(items)\n"));
  graph.Build();

  auto deps = graph.DependenciesOf("main.py::run");
  EXPECT_TRUE(Contains(deps, "orders.py::calculate_total"));
}

TEST(DependencyGraphTest, UnrelatedChunksHaveNoEdges) {
  DependencyGraph graph;
  graph.AddChunk("a.py::foo", "a.py", MakeChunk("function", "foo", "def foo():\n    return 1\n"));
  graph.AddChunk("b.py::bar", "b.py", MakeChunk("function", "bar", "def bar():\n    return 2\n"));
  graph.Build();

  EXPECT_TRUE(graph.DependenciesOf("a.py::foo").empty());
  EXPECT_TRUE(graph.DependentsOf("b.py::bar").empty());
}

TEST(DependencyGraphTest, UnknownChunkKeyReturnsEmpty) {
  DependencyGraph graph;
  graph.Build();

  EXPECT_TRUE(graph.DependenciesOf("nonexistent::key").empty());
  EXPECT_TRUE(graph.DependentsOf("nonexistent::key").empty());
}
