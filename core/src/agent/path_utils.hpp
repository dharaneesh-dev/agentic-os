#pragma once

#include <algorithm>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace cooper::core::agent::detail {

// weakly_canonical (not canonical) so this also works when the tail of relative_path doesn't
// exist yet, e.g. a write_file call creating a brand-new file.
inline std::filesystem::path ResolveInRepo(const std::filesystem::path& repo_root,
                                            const std::string& relative_path) {
  std::filesystem::path repo_root_normal = std::filesystem::weakly_canonical(repo_root);
  std::filesystem::path candidate = std::filesystem::weakly_canonical(repo_root_normal / relative_path);

  auto mismatch = std::mismatch(repo_root_normal.begin(), repo_root_normal.end(), candidate.begin(), candidate.end());
  if (mismatch.first != repo_root_normal.end()) {
    throw std::runtime_error("path escapes repository root: " + relative_path);
  }
  return candidate;
}

}  // namespace cooper::core::agent::detail
