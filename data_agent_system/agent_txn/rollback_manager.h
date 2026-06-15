#pragma once

#include <algorithm>
#include <iterator>
#include <string>

#include "data_agent_system/agent_txn/agent_txn_context.h"
#include "data_agent_system/branch/branch_context.h"

namespace data_agent_system::agent_txn {

class RollbackManager {
 public:
  void DiscardBranch(data_agent_system::branch::BranchContext& branch) const {
    branch.write_buffer.Truncate(0);
    branch.intent_log.Truncate(0);
    branch.savepoints.clear();
    branch.status = data_agent_system::branch::BranchStatus::kDiscarded;
  }

  bool RollbackToSavepoint(data_agent_system::branch::BranchContext& branch,
                           const std::string& savepoint_id) const {
    const auto it = std::find_if(
        branch.savepoints.begin(), branch.savepoints.end(),
        [&](const data_agent_system::cache::Savepoint& savepoint) {
          return savepoint.savepoint_id == savepoint_id;
        });
    if (it == branch.savepoints.end()) {
      return false;
    }
    branch.write_buffer.Truncate(it->write_log_position);
    branch.intent_log.Truncate(it->intent_log_position);
    branch.savepoints.erase(std::next(it), branch.savepoints.end());
    return true;
  }

  void AbortTransaction(AgentTxnContext& txn) const {
    txn.status = TxnStatus::kAborted;
    for (auto& branch : txn.branches) {
      DiscardBranch(branch);
    }
  }
};

}  // namespace data_agent_system::agent_txn
