#pragma once

#include <dirent.h>

#include <algorithm>
#include <string>
#include <vector>

#include "data_agent_system/agent_txn/commit_log_recovery.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::agent_txn {

class RecoveryManager {
 public:
  std::vector<std::string> ListCommitLogs(const std::string& commit_log_dir) const {
    std::vector<std::string> paths;
    DIR* dir = opendir(commit_log_dir.c_str());
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
      if (!EndsWith(name, ".commit.log")) {
        continue;
      }
      paths.push_back(commit_log_dir + "/" + name);
    }
    closedir(dir);
    std::sort(paths.begin(), paths.end());
    return paths;
  }

  RecoveryResult RecoverFromDirectory(
      const std::string& commit_log_dir,
      data_agent_system::storage::VersionedKVStore& store) const {
    return RecoverCommittedWrites(ListCommitLogs(commit_log_dir), store);
  }

 private:
  static bool EndsWith(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
      return false;
    }
    return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
  }
};

}  // namespace data_agent_system::agent_txn
