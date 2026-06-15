#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data_agent_system/branch/branch_context.h"

namespace data_agent_system::agent_txn {

enum class TxnStatus {
  kRunning,
  kCommitted,
  kAborted,
};

struct ValidationResult {
  bool success = false;
  std::string reason;
};

struct CommitLogEntry {
  std::string key;
  std::string value;
  std::uint64_t expected_version = 0;
};

struct CommitLog {
  std::vector<CommitLogEntry> entries;
};

struct TxnMetrics {
  std::size_t branch_count = 0;
  std::size_t planned_loser_count = 0;
  std::size_t winner_commit_count = 0;
  std::size_t real_abort_count = 0;
  std::size_t conflict_abort_count = 0;
  std::size_t validation_fail_count = 0;
  std::size_t retry_count = 0;
  double commit_latency_us = 0.0;
};

struct AgentTxnContext {
  std::string txn_id;
  std::string task_id;
  TxnStatus status = TxnStatus::kRunning;
  std::vector<data_agent_system::branch::BranchContext> branches;
  std::string winner_branch_id;
  CommitLog commit_log;
  ValidationResult validation_result;
  TxnMetrics metrics;
};

}  // namespace data_agent_system::agent_txn
