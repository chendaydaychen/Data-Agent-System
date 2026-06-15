#include <iostream>
#include <string>

#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/memory_kv_store.h"
#include "data_agent_system/workloads/register_builtin_recovery_handlers.h"

int main(int argc, char** argv) {
  if (argc != 2) {
    std::cerr << "usage: task_runtime_recovery_continue_demo <task_log_dir>\n";
    return 1;
  }

  data_agent_system::storage::MemoryKVStore store;
  store.Put("input:task_2", "seed_2_retry");
  store.Put("output:task_2", "draft_2_retry");
  store.Put("input:task_resume", "seed_resume_1");
  store.Put("input:task_resume", "seed_resume_2");
  store.Put("input:task_resume", "seed_resume_3");
  store.Put("input:task_resume", "seed_resume");

  data_agent_system::runtime::TaskRuntime runtime(store);
  data_agent_system::workloads::RegisterBuiltinRecoveryHandlers(runtime);
  auto executions = runtime.BuildRecoveryExecutionsFromDirectory(argv[1]);

  for (auto& execution : executions) {
    const bool committed = runtime.ContinueRecoveredTask(&execution);
    if (execution.recovery_plan.decision.task_id == "task_2") {
      std::cout << "task_2" << '\t'
                << committed << '\t'
                << execution.session.txn.winner_branch_id << '\t'
                << execution.session.txn.commit_log.entries.size() << '\t'
                << store.Get("output:task_2").value << '\n';
    } else if (execution.recovery_plan.decision.task_id == "task_resume") {
      std::cout << "task_resume" << '\t'
                << committed << '\t'
                << execution.session.txn.winner_branch_id << '\t'
                << execution.session.txn.commit_log.entries.size() << '\t'
                << store.Get("output:task_resume").value << '\n';
    }
  }

  return 0;
}
