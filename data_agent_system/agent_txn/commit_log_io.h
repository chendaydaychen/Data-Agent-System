#pragma once

#include <fstream>
#include <string>

#include "data_agent_system/agent_txn/agent_txn_context.h"

namespace data_agent_system::agent_txn {

inline const char* kCommitLogHeader = "DAS_COMMIT_LOG_V1";

inline std::string EscapeCommitLogText(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  for (const char ch : input) {
    if (ch == '\\' || ch == '\t' || ch == '\n') {
      output.push_back('\\');
      switch (ch) {
        case '\\':
          output.push_back('\\');
          break;
        case '\t':
          output.push_back('t');
          break;
        case '\n':
          output.push_back('n');
          break;
      }
    } else {
      output.push_back(ch);
    }
  }
  return output;
}

inline std::string UnescapeCommitLogText(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  bool escaped = false;
  for (const char ch : input) {
    if (escaped) {
      switch (ch) {
        case 't':
          output.push_back('\t');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case '\\':
          output.push_back('\\');
          break;
        default:
          output.push_back(ch);
          break;
      }
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else {
      output.push_back(ch);
    }
  }
  if (escaped) {
    output.push_back('\\');
  }
  return output;
}

inline const char* TxnStatusName(TxnStatus status) {
  switch (status) {
    case TxnStatus::kRunning:
      return "RUNNING";
    case TxnStatus::kCommitted:
      return "COMMITTED";
    case TxnStatus::kAborted:
      return "ABORTED";
  }
  return "UNKNOWN";
}

inline bool WriteCommitLogArtifact(const AgentTxnContext& txn, const std::string& output_path) {
  std::ofstream output(output_path.c_str(), std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  output << kCommitLogHeader << '\n';
  output << "txn_id=" << EscapeCommitLogText(txn.txn_id) << '\n';
  output << "task_id=" << EscapeCommitLogText(txn.task_id) << '\n';
  output << "status=" << TxnStatusName(txn.status) << '\n';
  output << "winner_branch_id=" << EscapeCommitLogText(txn.winner_branch_id) << '\n';
  output << "validation_success=" << (txn.validation_result.success ? 1 : 0) << '\n';
  output << "validation_reason=" << EscapeCommitLogText(txn.validation_result.reason) << '\n';
  output << "branch_count=" << txn.metrics.branch_count << '\n';
  output << "planned_loser_count=" << txn.metrics.planned_loser_count << '\n';
  output << "winner_commit_count=" << txn.metrics.winner_commit_count << '\n';
  output << "real_abort_count=" << txn.metrics.real_abort_count << '\n';
  output << "conflict_abort_count=" << txn.metrics.conflict_abort_count << '\n';
  output << "validation_fail_count=" << txn.metrics.validation_fail_count << '\n';
  output << "retry_count=" << txn.metrics.retry_count << '\n';
  output << "commit_latency_us=" << txn.metrics.commit_latency_us << '\n';
  output << "entry_count=" << txn.commit_log.entries.size() << '\n';
  output << "[entries]\n";
  for (const auto& entry : txn.commit_log.entries) {
    output << EscapeCommitLogText(entry.key) << '\t'
           << entry.expected_version << '\t'
           << EscapeCommitLogText(entry.value) << '\n';
  }
  output.flush();
  return output.good();
}

}  // namespace data_agent_system::agent_txn
