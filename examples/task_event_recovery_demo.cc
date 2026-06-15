#include <iostream>
#include <string>

#include "data_agent_system/runtime/task_recovery_manager.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: task_event_recovery_demo <task_log_dir>\n";
    return 1;
  }

  data_agent_system::runtime::TaskRecoveryManager manager;
  const auto decisions = manager.RecoverFromDirectory(argv[1]);
  for (const auto& decision : decisions) {
    std::cout << decision.task_id << '\t'
              << decision.txn_id << '\t'
              << data_agent_system::runtime::TaskRecoveryActionName(decision.action) << '\t'
              << decision.resume_branch_id << '\t'
              << decision.resume_savepoint_id << '\t'
              << decision.reason << '\t'
              << decision.source_log_path << '\n';
  }
  return 0;
}
