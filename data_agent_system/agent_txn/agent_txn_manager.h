#pragma once

#include <string>

#include "data_agent_system/agent_txn/agent_txn_context.h"
#include "data_agent_system/agent_txn/commit_manager.h"
#include "data_agent_system/agent_txn/recovery_manager.h"
#include "data_agent_system/agent_txn/rollback_manager.h"
#include "data_agent_system/agent_txn/validation_manager.h"
#include "data_agent_system/branch/branch_manager.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::agent_txn {

class AgentTxnManager {
 public:
  AgentTxnContext Begin(const std::string& txn_id, const std::string& task_id) const {
    AgentTxnContext txn;
    txn.txn_id = txn_id;
    txn.task_id = task_id;
    txn.status = TxnStatus::kRunning;
    return txn;
  }

  data_agent_system::branch::BranchContext& CreateBranch(AgentTxnContext& txn,
                                                         const std::string& branch_id) const {
    return branch_manager_.CreateBranch(txn.branches, branch_id);
  }

  bool SelectWinnerAndCommit(AgentTxnContext& txn,
                             data_agent_system::storage::VersionedKVStore& store) const {
    txn.winner_branch_id = branch_manager_.SelectWinner(txn.branches);
    branch_manager_.MarkWinnerAndLosers(txn.branches, txn.winner_branch_id);

    auto* winner = branch_manager_.FindBranch(txn.branches, txn.winner_branch_id);
    if (winner == nullptr) {
      txn.validation_result.success = false;
      txn.validation_result.reason = "winner branch not found";
      rollback_manager_.AbortTransaction(txn);
      return false;
    }

    txn.validation_result = validation_manager_.ValidateWinner(*winner, store);
    if (!txn.validation_result.success) {
      rollback_manager_.AbortTransaction(txn);
      return false;
    }

    ValidationResult commit_result;
    if (!commit_manager_.CommitWinner(txn, *winner, store, &commit_result)) {
      txn.validation_result = commit_result;
      rollback_manager_.AbortTransaction(txn);
      return false;
    }

    txn.status = TxnStatus::kCommitted;
    winner->status = data_agent_system::branch::BranchStatus::kCommitted;
    for (auto& branch : txn.branches) {
      if (branch.branch_id != txn.winner_branch_id) {
        rollback_manager_.DiscardBranch(branch);
      }
    }
    return true;
  }

  RecoveryResult RecoverCommittedWrites(
      const std::string& commit_log_dir,
      data_agent_system::storage::VersionedKVStore& store) const {
    return recovery_manager_.RecoverFromDirectory(commit_log_dir, store);
  }

  FallbackRecoveryResult RecoverFallbackCommits(
      const std::string& artifact_dir,
      data_agent_system::storage::VersionedKVStore& store) const {
    return recovery_manager_.RecoverFallbackCommitsFromDirectory(artifact_dir, store);
  }

 private:
  data_agent_system::branch::BranchManager branch_manager_;
  ValidationManager validation_manager_;
  CommitManager commit_manager_;
  RecoveryManager recovery_manager_;
  RollbackManager rollback_manager_;
};

}  // namespace data_agent_system::agent_txn
