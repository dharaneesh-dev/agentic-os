#pragma once

#include <filesystem>
#include <memory>
#include <string>

typedef struct git_repository git_repository;

namespace cooper::core::git {

class Repository {
 public:
  static Repository Init(const std::filesystem::path& path);
  static Repository Clone(const std::string& url, const std::filesystem::path& into);
  static Repository Open(const std::filesystem::path& path);

  void CreateBranch(const std::string& name);
  void CheckoutBranch(const std::string& name);
  void StageAll();
  void Commit(const std::string& message, const std::string& author_name, const std::string& author_email);
  std::string CurrentBranch() const;

 private:
  explicit Repository(git_repository* handle);

  struct GitRepositoryDeleter {
    void operator()(git_repository* handle) const;
  };

  std::unique_ptr<git_repository, GitRepositoryDeleter> handle_;
};

}  // namespace cooper::core::git
