#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "data_agent_system/agent_txn/agent_txn_context.h"
#include "data_agent_system/agent_txn/fallback_commit_log.h"
#include "data_agent_system/agent_txn/fallback_commit_recovery.h"
#include "data_agent_system/branch/branch_context.h"
#include "data_agent_system/intent/policy_dispatcher.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::agent_txn {

class CommitManager {
 public:
  bool CommitWinner(AgentTxnContext& txn,
                    const data_agent_system::branch::BranchContext& winner,
                    data_agent_system::storage::VersionedKVStore& store,
                    ValidationResult* failure) const {
    std::vector<data_agent_system::storage::VersionCheck> checks;
    std::unordered_map<std::string, std::uint64_t> seen_checks;
    for (const auto& read : winner.read_set.Entries()) {
      const auto [it, inserted] = seen_checks.emplace(read.object_id, read.observed_version);
      if (!inserted && it->second != read.observed_version) {
        if (failure != nullptr) {
          failure->success = false;
          failure->reason = "inconsistent read versions for " + read.object_id;
        }
        return false;
      }
    }
    for (const auto& [key, version] : seen_checks) {
      data_agent_system::storage::VersionCheck check;
      check.key = key;
      check.expected_version = version;
      checks.push_back(check);
    }

    std::vector<data_agent_system::storage::WriteOp> writes;
    const auto entries = winner.write_buffer.Entries();
    const auto& intents = winner.intent_log.Entries();
    std::unordered_map<std::string, data_agent_system::intent::WriteIntent> latest_intents;
    for (const auto& intent : intents) {
      latest_intents[intent.object_id] = intent;
    }

    for (const auto& entry : entries) {
      const auto intent_it = latest_intents.find(entry.object_id);
      if (intent_it == latest_intents.end()) {
        if (failure != nullptr) {
          failure->success = false;
          failure->reason = "missing latest intent for " + entry.object_id;
        }
        return false;
      }
      const auto current = store.Get(entry.object_id);
      const auto resolved =
          data_agent_system::intent::PolicyDispatcher::ResolveWrite(entry, intent_it->second,
                                                                    current.value);
      if (!resolved.success) {
        if (failure != nullptr) {
          failure->success = false;
          failure->reason = resolved.reason.empty()
                                ? "failed to resolve write intent for " + entry.object_id
                                : resolved.reason + " for " + entry.object_id;
        }
        return false;
      }
      if (!resolved.should_write) {
        continue;
      }
      const bool semantic_rebase =
          data_agent_system::intent::PolicyDispatcher::UsesSemanticRebase(
              intent_it->second.intent_type);
      const std::uint64_t expected_version =
          semantic_rebase ? current.version : entry.base_version;
      if (semantic_rebase) {
        seen_checks[entry.object_id] = expected_version;
      } else {
        AddOrVerifyCheck(entry.object_id, expected_version, &seen_checks, failure);
      }
      if (failure != nullptr && !failure->success && !failure->reason.empty()) {
        return false;
      }
      data_agent_system::storage::WriteOp write;
      write.key = entry.object_id;
      write.value = resolved.value;
      writes.push_back(write);
    }

    checks.clear();
    for (const auto& [key, version] : seen_checks) {
      data_agent_system::storage::VersionCheck check;
      check.key = key;
      check.expected_version = version;
      checks.push_back(check);
    }

    const bool committed =
        store.SupportsAtomicBatchConditionalWrite()
            ? store.BatchPutIfVersion(checks, writes)
            : CommitWinnerWithoutBatch(txn, checks, writes, store, failure);
    if (!committed) {
      if (failure != nullptr) {
        if (failure->reason.empty()) {
          failure->success = false;
          failure->reason = store.SupportsAtomicBatchConditionalWrite()
                                ? "batch conditional write failed"
                                : "fallback conditional write failed";
        }
      }
      return false;
    }

    txn.commit_log.entries.clear();
    for (const auto& write : writes) {
      CommitLogEntry entry;
      entry.key = write.key;
      entry.value = write.value;
      entry.expected_version = seen_checks[write.key];
      txn.commit_log.entries.push_back(entry);
    }
    return true;
  }

 private:
  struct AppliedWrite {
    std::string key;
    std::string previous_value;
    std::uint64_t previous_version = 0;
    bool previous_exists = false;
  };

  static bool CommitWinnerWithoutBatch(
      const AgentTxnContext& txn,
      const std::vector<data_agent_system::storage::VersionCheck>& checks,
      const std::vector<data_agent_system::storage::WriteOp>& writes,
      data_agent_system::storage::VersionedKVStore& store,
      ValidationResult* failure) {
    if (!txn.fallback_commit.artifact_path.empty()) {
      return CommitWinnerWithoutBatchCrashSafe(txn, checks, writes, store, failure);
    }

    std::unordered_map<std::string, std::uint64_t> expected_versions;
    for (const auto& check : checks) {
      expected_versions[check.key] = check.expected_version;
    }

    std::vector<AppliedWrite> applied_writes;
    applied_writes.reserve(writes.size());
    for (const auto& write : writes) {
      const auto expected_it = expected_versions.find(write.key);
      if (expected_it == expected_versions.end()) {
        if (failure != nullptr) {
          failure->success = false;
          failure->reason = "missing expected version for " + write.key;
        }
        return false;
      }

      const auto current = store.Get(write.key);
      if (current.version != expected_it->second) {
        RollbackAppliedWrites(applied_writes, store, failure);
        if (failure != nullptr && failure->reason.empty()) {
          failure->success = false;
          failure->reason = "fallback write version changed for " + write.key;
        }
        return false;
      }

      if (!store.PutIfVersion(write.key, expected_it->second, write.value)) {
        RollbackAppliedWrites(applied_writes, store, failure);
        if (failure != nullptr && failure->reason.empty()) {
          failure->success = false;
          failure->reason = "fallback conditional write failed for " + write.key;
        }
        return false;
      }

      AppliedWrite applied;
      applied.key = write.key;
      applied.previous_value = current.value;
      applied.previous_version = current.version;
      applied.previous_exists = current.exists;
      applied_writes.push_back(applied);
    }

    return true;
  }

  static bool CommitWinnerWithoutBatchCrashSafe(
      const AgentTxnContext& txn,
      const std::vector<data_agent_system::storage::VersionCheck>& checks,
      const std::vector<data_agent_system::storage::WriteOp>& writes,
      data_agent_system::storage::VersionedKVStore& store,
      ValidationResult* failure) {
    std::unordered_map<std::string, std::uint64_t> expected_versions;
    for (const auto& check : checks) {
      expected_versions[check.key] = check.expected_version;
    }

    FallbackCommitArtifact artifact;
    artifact.txn_id = txn.txn_id;
    artifact.task_id = txn.task_id;
    artifact.phase = FallbackCommitPhase::kPrepared;
    artifact.entries.reserve(writes.size());
    for (const auto& write : writes) {
      const auto expected_it = expected_versions.find(write.key);
      if (expected_it == expected_versions.end()) {
        if (failure != nullptr) {
          failure->success = false;
          failure->reason = "missing expected version for " + write.key;
        }
        return false;
      }

      const auto current = store.Get(write.key);
      if (current.version != expected_it->second) {
        if (failure != nullptr) {
          failure->success = false;
          failure->reason = "fallback write version changed for " + write.key;
        }
        return false;
      }

      FallbackCommitEntry entry;
      entry.key = write.key;
      entry.expected_version = expected_it->second;
      entry.target_value = write.value;
      entry.previous_value = current.value;
      entry.previous_version = current.version;
      entry.previous_exists = current.exists;
      artifact.entries.push_back(entry);
    }

    if (!WriteFallbackCommitArtifact(artifact, txn.fallback_commit.artifact_path)) {
      if (failure != nullptr) {
        failure->success = false;
        failure->reason = "failed to persist fallback prepare artifact";
      }
      return false;
    }

    for (const auto& entry : artifact.entries) {
      const auto current = store.Get(entry.key);
      if (current.version != entry.expected_version || !current.exists) {
        if (failure != nullptr) {
          failure->success = false;
          failure->reason = "fallback write version changed for " + entry.key;
        }
        return RollbackCrashSafeArtifact(&artifact, txn.fallback_commit.artifact_path, store,
                                         failure);
      }
      if (!store.PutIfVersion(entry.key, entry.expected_version, entry.target_value)) {
        if (failure != nullptr) {
          failure->success = false;
          failure->reason = "fallback conditional write failed for " + entry.key;
        }
        return RollbackCrashSafeArtifact(&artifact, txn.fallback_commit.artifact_path, store,
                                         failure);
      }

      artifact.applied_count += 1;
      artifact.phase = FallbackCommitPhase::kApplying;
      if (!WriteFallbackCommitArtifact(artifact, txn.fallback_commit.artifact_path)) {
        if (failure != nullptr) {
          failure->success = false;
          failure->reason = "failed to persist fallback apply progress";
        }
        return false;
      }
      if (txn.fallback_commit.simulate_crash_after_apply_count > 0 &&
          artifact.applied_count == txn.fallback_commit.simulate_crash_after_apply_count) {
        if (failure != nullptr) {
          failure->success = false;
          failure->reason = "simulated crash during fallback commit";
        }
        return false;
      }
    }

    artifact.phase = FallbackCommitPhase::kCommitted;
    if (!WriteFallbackCommitArtifact(artifact, txn.fallback_commit.artifact_path)) {
      if (failure != nullptr) {
        failure->success = false;
        failure->reason = "failed to persist fallback commit state";
      }
      return false;
    }
    return true;
  }

  static bool RollbackAppliedWrites(const std::vector<AppliedWrite>& applied_writes,
                                    data_agent_system::storage::VersionedKVStore& store,
                                    ValidationResult* failure) {
    for (auto it = applied_writes.rbegin(); it != applied_writes.rend(); ++it) {
      const std::uint64_t rollback_expected_version = it->previous_version + 1;
      const bool rolled_back =
          it->previous_exists
              ? store.PutIfVersion(it->key, rollback_expected_version, it->previous_value)
              : store.DeleteIfVersion(it->key, rollback_expected_version);
      if (!rolled_back) {
        if (failure != nullptr) {
          failure->success = false;
          failure->reason = "fallback rollback failed for " + it->key;
        }
        return false;
      }
    }
    return true;
  }

  static bool RollbackCrashSafeArtifact(FallbackCommitArtifact* artifact,
                                        const std::string& artifact_path,
                                        data_agent_system::storage::VersionedKVStore& store,
                                        ValidationResult* failure) {
    FallbackRecoveryResult recovery;
    if (!ResumeRollbackFallbackCommit(artifact, artifact_path, store, &recovery)) {
      if (failure != nullptr && failure->reason.empty()) {
        failure->success = false;
        failure->reason =
            recovery.reason.empty() ? "fallback rollback failed" : recovery.reason;
      }
      return false;
    }
    return false;
  }

  static void AddOrVerifyCheck(const std::string& object_id,
                               std::uint64_t version,
                               std::unordered_map<std::string, std::uint64_t>* seen_checks,
                               ValidationResult* failure) {
    const auto [it, inserted] = seen_checks->emplace(object_id, version);
    if (!inserted && it->second != version && failure != nullptr) {
      failure->success = false;
      failure->reason = "conflicting expected versions for " + object_id;
    }
  }
};

}  // namespace data_agent_system::agent_txn
