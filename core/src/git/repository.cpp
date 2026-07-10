#include "cooper/core/git/repository.hpp"

#include <git2.h>

#include <stdexcept>

namespace cooper::core::git {

namespace {

std::string ToUtf8(const std::filesystem::path& path) {
  const std::u8string u8 = path.u8string();
  return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

class LibGit2Guard {
 public:
  LibGit2Guard() { git_libgit2_init(); }
  ~LibGit2Guard() { git_libgit2_shutdown(); }
};

void EnsureLibGit2Initialized() {
  static LibGit2Guard guard;
  (void)guard;
}

void ThrowIfError(int error_code, const std::string& context) {
  if (error_code != 0) {
    const git_error* err = git_error_last();
    std::string message = (err != nullptr && err->message != nullptr) ? err->message : "unknown libgit2 error";
    throw std::runtime_error(context + ": " + message);
  }
}

}  // namespace

void Repository::GitRepositoryDeleter::operator()(git_repository* handle) const {
  git_repository_free(handle);
}

Repository::Repository(git_repository* handle) : handle_(handle) {}

Repository Repository::Init(const std::filesystem::path& path) {
  EnsureLibGit2Initialized();
  git_repository* repo = nullptr;
  ThrowIfError(git_repository_init(&repo, ToUtf8(path).c_str(), 0), "Repository::Init");
  return Repository(repo);
}

Repository Repository::Clone(const std::string& url, const std::filesystem::path& into) {
  EnsureLibGit2Initialized();
  git_repository* repo = nullptr;
  ThrowIfError(git_clone(&repo, url.c_str(), ToUtf8(into).c_str(), nullptr), "Repository::Clone");
  return Repository(repo);
}

Repository Repository::Open(const std::filesystem::path& path) {
  EnsureLibGit2Initialized();
  git_repository* repo = nullptr;
  ThrowIfError(git_repository_open(&repo, ToUtf8(path).c_str()), "Repository::Open");
  return Repository(repo);
}

void Repository::CreateBranch(const std::string& name) {
  git_oid head_oid;
  ThrowIfError(git_reference_name_to_id(&head_oid, handle_.get(), "HEAD"), "Repository::CreateBranch resolve HEAD");

  git_commit* commit = nullptr;
  ThrowIfError(git_commit_lookup(&commit, handle_.get(), &head_oid), "Repository::CreateBranch lookup commit");

  git_reference* branch_ref = nullptr;
  int rc = git_branch_create(&branch_ref, handle_.get(), name.c_str(), commit, 0);

  if (branch_ref != nullptr) {
    git_reference_free(branch_ref);
  }
  git_commit_free(commit);
  ThrowIfError(rc, "Repository::CreateBranch");
}

void Repository::CheckoutBranch(const std::string& name) {
  std::string ref_name = "refs/heads/" + name;

  git_reference* ref = nullptr;
  ThrowIfError(git_reference_lookup(&ref, handle_.get(), ref_name.c_str()), "Repository::CheckoutBranch lookup");

  git_object* target = nullptr;
  int rc = git_reference_peel(&target, ref, GIT_OBJECT_COMMIT);
  git_reference_free(ref);
  ThrowIfError(rc, "Repository::CheckoutBranch peel");

  git_checkout_options options = GIT_CHECKOUT_OPTIONS_INIT;
  options.checkout_strategy = GIT_CHECKOUT_SAFE;
  rc = git_checkout_tree(handle_.get(), target, &options);
  git_object_free(target);
  ThrowIfError(rc, "Repository::CheckoutBranch checkout_tree");

  ThrowIfError(git_repository_set_head(handle_.get(), ref_name.c_str()), "Repository::CheckoutBranch set_head");
}

void Repository::StageAll() {
  git_index* index = nullptr;
  ThrowIfError(git_repository_index(&index, handle_.get()), "Repository::StageAll open index");

  char wildcard[] = "*";
  char* paths[] = {wildcard};
  git_strarray pathspec{paths, 1};

  int rc = git_index_add_all(index, &pathspec, GIT_INDEX_ADD_DEFAULT, nullptr, nullptr);
  if (rc == 0) {
    rc = git_index_write(index);
  }
  git_index_free(index);
  ThrowIfError(rc, "Repository::StageAll");
}

void Repository::Commit(const std::string& message, const std::string& author_name, const std::string& author_email) {
  git_index* index = nullptr;
  ThrowIfError(git_repository_index(&index, handle_.get()), "Repository::Commit open index");

  git_oid tree_oid;
  int rc = git_index_write_tree(&tree_oid, index);
  git_index_free(index);
  ThrowIfError(rc, "Repository::Commit write_tree");

  git_tree* tree = nullptr;
  ThrowIfError(git_tree_lookup(&tree, handle_.get(), &tree_oid), "Repository::Commit lookup tree");

  git_signature* signature = nullptr;
  rc = git_signature_now(&signature, author_name.c_str(), author_email.c_str());
  if (rc != 0) {
    git_tree_free(tree);
    ThrowIfError(rc, "Repository::Commit signature");
  }

  git_commit* parent_commit = nullptr;
  git_oid head_oid;
  bool has_head = git_reference_name_to_id(&head_oid, handle_.get(), "HEAD") == 0;
  if (has_head) {
    ThrowIfError(git_commit_lookup(&parent_commit, handle_.get(), &head_oid), "Repository::Commit lookup parent");
  }

  git_oid commit_oid;
  const git_commit* parents[1] = {parent_commit};
  rc = git_commit_create(&commit_oid, handle_.get(), "HEAD", signature, signature, nullptr, message.c_str(), tree,
                          has_head ? 1 : 0, has_head ? parents : nullptr);

  if (parent_commit != nullptr) {
    git_commit_free(parent_commit);
  }
  git_signature_free(signature);
  git_tree_free(tree);
  ThrowIfError(rc, "Repository::Commit create");
}

std::string Repository::CurrentBranch() const {
  int unborn = git_repository_head_unborn(handle_.get());
  if (unborn == 1) {
    git_reference* head_ref = nullptr;
    ThrowIfError(git_reference_lookup(&head_ref, handle_.get(), "HEAD"), "Repository::CurrentBranch lookup unborn HEAD");
    const char* target = git_reference_symbolic_target(head_ref);
    std::string full = target != nullptr ? target : "";
    git_reference_free(head_ref);
    auto pos = full.rfind('/');
    return pos == std::string::npos ? full : full.substr(pos + 1);
  }

  git_reference* head = nullptr;
  ThrowIfError(git_repository_head(&head, handle_.get()), "Repository::CurrentBranch");
  const char* shorthand = git_reference_shorthand(head);
  std::string branch = shorthand != nullptr ? shorthand : "";
  git_reference_free(head);
  return branch;
}

}  // namespace cooper::core::git
