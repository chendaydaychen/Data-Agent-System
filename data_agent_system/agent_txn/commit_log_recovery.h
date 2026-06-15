#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "data_agent_system/agent_txn/commit_log_replay.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::agent_txn {

struct RecoveryResult {
  std::size_t applied_entry_count = 0;
  std::size_t skipped_committed_entry_count = 0;
  std::size_t skipped_non_committed_log_count = 0;
  std::size_t parse_failure_count = 0;
  std::size_t conflict_count = 0;
  bool success = true;
  std::string reason;
};

inline RecoveryResult RecoverCommittedWrites(
    const std::vector<std::string>& commit_log_paths,
    data_agent_system::storage::VersionedKVStore& store) {
  RecoveryResult result;

  for (const auto& path : commit_log_paths) {
    ParsedCommitLogArtifact artifact;
    if (!ParseCommitLogArtifact(path, &artifact)) {
      result.parse_failure_count += 1;
      result.success = false;
      result.reason = "failed to parse commit log: " + path;
      return result;
    }

    const auto status_it = artifact.metadata.find("status");
    if (status_it == artifact.metadata.end() || status_it->second != "COMMITTED") {
      result.skipped_non_committed_log_count += 1;
      continue;
    }

    for (const auto& entry : artifact.entries) {
      const auto current = store.Get(entry.key);

      if (current.exists && current.version == entry.expected_version + 1 &&
          current.value == entry.value) {
        result.skipped_committed_entry_count += 1;
        continue;
      }

      if (!store.PutIfVersion(entry.key, entry.expected_version, entry.value)) {
        result.conflict_count += 1;
        result.success = false;
        result.reason = "recovery version conflict for key: " + entry.key;
        return result;
      }
      result.applied_entry_count += 1;
    }
  }

  return result;
}

}  // namespace data_agent_system::agent_txn
