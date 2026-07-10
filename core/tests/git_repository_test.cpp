#include "cooper/core/git/repository.hpp"

#include <gtest/gtest.h>

#include <fstream>
#include <random>

using cooper::core::git::Repository;

namespace {

std::filesystem::path MakeTempDir() {
  std::random_device rd;
  std::filesystem::path dir = std::filesystem::temp_directory_path() /
                               ("cooper_core_git_test_" + std::to_string(rd()));
  std::filesystem::create_directories(dir);
  return dir;
}

}  // namespace

class GitRepositoryTest : public ::testing::Test {
 protected:
  void SetUp() override { temp_dir_ = MakeTempDir(); }

  void TearDown() override { std::filesystem::remove_all(temp_dir_); }

  std::filesystem::path temp_dir_;
};

TEST_F(GitRepositoryTest, InitStageCommitAndBranch) {
  Repository repo = Repository::Init(temp_dir_);

  std::ofstream file(temp_dir_ / "hello.txt");
  file << "hello cooper\n";
  file.close();

  repo.StageAll();
  repo.Commit("initial commit", "Cooper Test", "cooper-test@example.com");

  std::string initial_branch = repo.CurrentBranch();
  EXPECT_FALSE(initial_branch.empty());

  repo.CreateBranch("feature/test");
  repo.CheckoutBranch("feature/test");

  EXPECT_EQ(repo.CurrentBranch(), "feature/test");
}

TEST_F(GitRepositoryTest, OpenExistingRepository) {
  {
    Repository repo = Repository::Init(temp_dir_);
    std::ofstream file(temp_dir_ / "hello.txt");
    file << "hello cooper\n";
    file.close();
    repo.StageAll();
    repo.Commit("initial commit", "Cooper Test", "cooper-test@example.com");
  }

  Repository reopened = Repository::Open(temp_dir_);
  EXPECT_FALSE(reopened.CurrentBranch().empty());
}

TEST_F(GitRepositoryTest, ThrowsWhenOpeningNonRepository) {
  EXPECT_THROW(Repository::Open(temp_dir_), std::runtime_error);
}
