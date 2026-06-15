#pragma once

#include <dirent.h>

#include <algorithm>
#include <string>
#include <vector>

#include "data_agent_system/agent_txn/commit_log_recovery.h"
#include "data_agent_system/agent_txn/fallback_commit_recovery.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::agent_txn {

class RecoveryManager {
 public:
  std::vector<std::string> ListCommitLogs(const std::string& commit_log_dir) const {
    return ListPathsWithSuffix(commit_log_dir, ".commit.log");
  }

  std::vector<std::string> ListFallbackCommitArtifacts(
      const std::string& artifact_dir) const {
    return ListPathsWithSuffix(artifact_dir, ".fallback.log");
  }

  RecoveryResult RecoverFromDirectory(
      const std::string& commit_log_dir,
      data_agent_system::storage::VersionedKVStore& store) const {
    return RecoverCommittedWrites(ListCommitLogs(commit_log_dir), store);
  }

  FallbackRecoveryResult RecoverFallbackCommitsFromDirectory(
      const std::string& artifact_dir,
      data_agent_system::storage::VersionedKVStore& store) const {
    return RecoverFallbackCommitArtifacts(ListFallbackCommitArtifacts(artifact_dir), store);
  }

 private:
  std::vector<std::string> ListPathsWithSuffix(const std::string& dir_path,
                                               const std::string& suffix) const {
    std::vector<std::string> paths;
    DIR* dir = opendir(dir_path.c_str());
    if (dir == nullptr) {
      return paths;
    }

    while (true) {
      dirent* entry = readdir(dir);
      if (entry == nullptr) {
        break;
      }

      const std::string name = entry->d_name;
      if (name == "." || name == "..") {
        continue;
      }
      if (!EndsWith(name, suffix)) {
        continue;
      }
      paths.push_back(dir_path + "/" + name);
    }
    closedir(dir);
    std::sort(paths.begin(), paths.end());
    return paths;
  }
  static bool EndsWith(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
      return false;
    }
    return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
  }
};

}  // namespace data_agent_system::agent_txn
