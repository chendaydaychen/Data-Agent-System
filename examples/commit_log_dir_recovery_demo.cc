#include <iostream>
#include <string>

#include "data_agent_system/agent_txn/agent_txn_manager.h"
#include "data_agent_system/storage/file_kv_store.h"

int main(int argc, char** argv) {
  if (argc != 3) {
    std::cerr << "usage: commit_log_dir_recovery_demo <store_path> <commit_log_dir>\n";
    return 1;
  }

  const std::string store_path = argv[1];
  const std::string commit_log_dir = argv[2];

  data_agent_system::storage::FileKVStore store(store_path);
  data_agent_system::agent_txn::AgentTxnManager txn_manager;
  const auto result = txn_manager.RecoverCommittedWrites(commit_log_dir, store);

  std::cout << "success=" << (result.success ? 1 : 0) << '\n';
  std::cout << "applied_entry_count=" << result.applied_entry_count << '\n';
  std::cout << "skipped_committed_entry_count=" << result.skipped_committed_entry_count << '\n';
  std::cout << "skipped_non_committed_log_count=" << result.skipped_non_committed_log_count << '\n';
  std::cout << "parse_failure_count=" << result.parse_failure_count << '\n';
  std::cout << "conflict_count=" << result.conflict_count << '\n';
  std::cout << "reason=" << result.reason << '\n';
  return result.success ? 0 : 1;
}
