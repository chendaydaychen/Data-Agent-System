#include <iostream>
#include <string>
#include <vector>

#include "data_agent_system/agent_txn/commit_log_recovery.h"
#include "data_agent_system/storage/file_kv_store.h"

int main(int argc, char** argv) {
  if (argc < 3) {
    std::cerr << "usage: commit_log_recovery_demo <store_path> <commit_log> [<commit_log> ...]\n";
    return 1;
  }

  const std::string store_path = argv[1];
  std::vector<std::string> commit_log_paths;
  for (int i = 2; i < argc; ++i) {
    commit_log_paths.push_back(argv[i]);
  }

  data_agent_system::storage::FileKVStore store(store_path);
  const auto result =
      data_agent_system::agent_txn::RecoverCommittedWrites(commit_log_paths, store);

  std::cout << "success=" << (result.success ? 1 : 0) << '\n';
  std::cout << "applied_entry_count=" << result.applied_entry_count << '\n';
  std::cout << "skipped_committed_entry_count=" << result.skipped_committed_entry_count << '\n';
  std::cout << "skipped_non_committed_log_count=" << result.skipped_non_committed_log_count << '\n';
  std::cout << "parse_failure_count=" << result.parse_failure_count << '\n';
  std::cout << "conflict_count=" << result.conflict_count << '\n';
  std::cout << "reason=" << result.reason << '\n';
  return result.success ? 0 : 1;
}
