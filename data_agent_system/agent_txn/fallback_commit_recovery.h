#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "data_agent_system/agent_txn/fallback_commit_log.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::agent_txn {

struct FallbackRecoveryResult {
  std::size_t recovered_artifact_count = 0;
  std::size_t skipped_terminal_artifact_count = 0;
  std::size_t rolled_forward_write_count = 0;
  std::size_t rolled_back_write_count = 0;
  std::size_t parse_failure_count = 0;
  std::size_t conflict_count = 0;
  bool success = true;
  std::string reason;
};

inline bool FallbackEntryMatchesTarget(
    const FallbackCommitEntry& entry,
    const data_agent_system::storage::VersionedValue& current) {
  return current.exists && current.version == entry.expected_version + 1 &&
         current.value == entry.target_value;
}

inline bool FallbackEntryMatchesPrevious(
    const FallbackCommitEntry& entry,
    const data_agent_system::storage::VersionedValue& current) {
  if (!entry.previous_exists) {
    return !current.exists && current.version == 0;
  }
  return current.exists && current.version == entry.previous_version &&
         current.value == entry.previous_value;
}

inline std::size_t DetectCommittedPrefix(
    const FallbackCommitArtifact& artifact,
    data_agent_system::storage::VersionedKVStore& store,
    FallbackRecoveryResult* result) {
  std::size_t committed_prefix = 0;
  while (committed_prefix < artifact.entries.size()) {
    const auto current = store.Get(artifact.entries[committed_prefix].key);
    if (!FallbackEntryMatchesTarget(artifact.entries[committed_prefix], current)) {
      break;
    }
    committed_prefix += 1;
  }

  for (std::size_t i = committed_prefix; i < artifact.entries.size(); ++i) {
    const auto current = store.Get(artifact.entries[i].key);
    if (!FallbackEntryMatchesPrevious(artifact.entries[i], current)) {
      if (result != nullptr) {
        result->success = false;
        result->conflict_count += 1;
        result->reason =
            "fallback recovery encountered non-prefix state at key: " + artifact.entries[i].key;
      }
      return artifact.entries.size() + 1;
    }
  }
  return committed_prefix;
}

inline bool ResumeForwardFallbackCommit(FallbackCommitArtifact* artifact,
                                        const std::string& artifact_path,
                                        data_agent_system::storage::VersionedKVStore& store,
                                        FallbackRecoveryResult* result) {
  if (artifact == nullptr) {
    return false;
  }

  const std::size_t committed_prefix = DetectCommittedPrefix(*artifact, store, result);
  if (committed_prefix > artifact->entries.size()) {
    return false;
  }

  artifact->applied_count = committed_prefix;
  artifact->phase = (committed_prefix == artifact->entries.size()) ? FallbackCommitPhase::kCommitted
                                                                   : FallbackCommitPhase::kApplying;
  if (!WriteFallbackCommitArtifact(*artifact, artifact_path)) {
    if (result != nullptr) {
      result->success = false;
      result->reason = "failed to persist fallback forward state: " + artifact_path;
    }
    return false;
  }

  for (std::size_t i = committed_prefix; i < artifact->entries.size(); ++i) {
    const auto& entry = artifact->entries[i];
    if (!store.PutIfVersion(entry.key, entry.expected_version, entry.target_value)) {
      if (result != nullptr) {
        result->success = false;
        result->conflict_count += 1;
        result->reason = "fallback recovery forward apply failed for key: " + entry.key;
      }
      return false;
    }
    artifact->applied_count = i + 1;
    artifact->phase = FallbackCommitPhase::kApplying;
    if (!WriteFallbackCommitArtifact(*artifact, artifact_path)) {
      if (result != nullptr) {
        result->success = false;
        result->reason = "failed to persist fallback forward progress: " + artifact_path;
      }
      return false;
    }
    if (result != nullptr) {
      result->rolled_forward_write_count += 1;
    }
  }

  artifact->phase = FallbackCommitPhase::kCommitted;
  if (!WriteFallbackCommitArtifact(*artifact, artifact_path)) {
    if (result != nullptr) {
      result->success = false;
      result->reason = "failed to persist fallback committed state: " + artifact_path;
    }
    return false;
  }
  return true;
}

inline bool ResumeRollbackFallbackCommit(FallbackCommitArtifact* artifact,
                                         const std::string& artifact_path,
                                         data_agent_system::storage::VersionedKVStore& store,
                                         FallbackRecoveryResult* result) {
  if (artifact == nullptr) {
    return false;
  }

  const std::size_t committed_prefix = DetectCommittedPrefix(*artifact, store, result);
  if (committed_prefix > artifact->entries.size()) {
    return false;
  }

  artifact->applied_count = committed_prefix;
  artifact->phase = FallbackCommitPhase::kRollingBack;
  if (!WriteFallbackCommitArtifact(*artifact, artifact_path)) {
    if (result != nullptr) {
      result->success = false;
      result->reason = "failed to persist fallback rollback state: " + artifact_path;
    }
    return false;
  }

  while (artifact->applied_count > 0) {
    const std::size_t entry_index = artifact->applied_count - 1;
    const auto& entry = artifact->entries[entry_index];
    const auto current = store.Get(entry.key);
    if (FallbackEntryMatchesPrevious(entry, current)) {
      artifact->applied_count -= 1;
      if (!WriteFallbackCommitArtifact(*artifact, artifact_path)) {
        if (result != nullptr) {
          result->success = false;
          result->reason = "failed to persist fallback rollback progress: " + artifact_path;
        }
        return false;
      }
      continue;
    }
    if (!FallbackEntryMatchesTarget(entry, current)) {
      if (result != nullptr) {
        result->success = false;
        result->conflict_count += 1;
        result->reason = "fallback recovery conflict for key: " + entry.key;
      }
      return false;
    }

    bool rolled_back = false;
    if (entry.previous_exists) {
      rolled_back = store.PutIfVersion(entry.key, entry.previous_version + 1, entry.previous_value);
    } else {
      rolled_back = store.DeleteIfVersion(entry.key, entry.expected_version + 1);
    }
    if (!rolled_back) {
      if (result != nullptr) {
        result->success = false;
        result->conflict_count += 1;
        result->reason = "fallback recovery rollback failed for key: " + entry.key;
      }
      return false;
    }

    artifact->applied_count -= 1;
    if (!WriteFallbackCommitArtifact(*artifact, artifact_path)) {
      if (result != nullptr) {
        result->success = false;
        result->reason = "failed to persist fallback rollback progress: " + artifact_path;
      }
      return false;
    }
    if (result != nullptr) {
      result->rolled_back_write_count += 1;
    }
  }

  artifact->phase = FallbackCommitPhase::kRolledBack;
  if (!WriteFallbackCommitArtifact(*artifact, artifact_path)) {
    if (result != nullptr) {
      result->success = false;
      result->reason = "failed to persist fallback terminal state: " + artifact_path;
    }
    return false;
  }
  return true;
}

inline FallbackRecoveryResult RecoverFallbackCommitArtifacts(
    const std::vector<std::string>& artifact_paths,
    data_agent_system::storage::VersionedKVStore& store) {
  FallbackRecoveryResult result;
  for (const auto& artifact_path : artifact_paths) {
    FallbackCommitArtifact artifact;
    if (!ParseFallbackCommitArtifact(artifact_path, &artifact)) {
      result.parse_failure_count += 1;
      result.success = false;
      result.reason = "failed to parse fallback artifact: " + artifact_path;
      return result;
    }

    if (artifact.phase == FallbackCommitPhase::kCommitted ||
        artifact.phase == FallbackCommitPhase::kRolledBack) {
      result.skipped_terminal_artifact_count += 1;
      continue;
    }

    const bool recovered =
        artifact.phase == FallbackCommitPhase::kRollingBack
            ? ResumeRollbackFallbackCommit(&artifact, artifact_path, store, &result)
            : ResumeForwardFallbackCommit(&artifact, artifact_path, store, &result);
    if (!recovered) {
      return result;
    }
    result.recovered_artifact_count += 1;
  }
  return result;
}

}  // namespace data_agent_system::agent_txn
