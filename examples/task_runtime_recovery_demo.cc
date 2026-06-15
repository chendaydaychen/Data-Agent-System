#include <iostream>
#include <string>

#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/memory_kv_store.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: task_runtime_recovery_demo <task_log_dir>\n";
    return 1;
  }

  data_agent_system::storage::MemoryKVStore store;
  data_agent_system::runtime::TaskRuntime runtime(store);
  auto recovery_executions = runtime.BuildRecoveryExecutionsFromDirectory(argv[1]);
  for (auto& execution : recovery_executions) {
    const bool executed = runtime.ExecuteRecoveryExecution(&execution);
    const auto& plan = execution.recovery_plan;
    const auto& session = execution.session;
    std::cout << session.task.task_id << '\t'
              << data_agent_system::runtime::TaskRecoveryActionName(plan.decision.action) << '\t'
              << data_agent_system::runtime::TaskRecoveryCommandTypeName(execution.command_type)
              << '\t'
              << execution.target_branch_id << '\t'
              << execution.target_savepoint_id << '\t'
              << executed << '\t'
              << session.plan.branch_plans.size() << '\t'
              << session.events.size() << '\t'
              << session.commit_attempts << '\t'
              << static_cast<int>(session.task.phase) << '\t'
              << execution.summary << '\n';
  }
  return 0;
}
