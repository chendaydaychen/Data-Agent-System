#include <iostream>
#include <string>

#include "data_agent_system/runtime/task_event_log_io.h"
#include "data_agent_system/runtime/task_recovery_execution.h"
#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/memory_kv_store.h"
#include "data_agent_system/workloads/agent_task/minimal_candidate_task.h"
#include "data_agent_system/workloads/register_builtin_recovery_handlers.h"

int main() {
  using data_agent_system::runtime::TaskRuntime;
  using data_agent_system::storage::MemoryKVStore;

  MemoryKVStore store;
  store.Put("input:retry_task", "seed");
  store.Put("output:retry_task", "draft");

  TaskRuntime initial_runtime(store);
  auto failed_session = initial_runtime.SubmitTask(
      data_agent_system::workloads::agent_task::BuildMinimalCandidateTaskContext(
          "retry_task", "input:retry_task", "output:retry_task"),
      data_agent_system::workloads::agent_task::BuildMinimalCandidateExecutionPlan());
  data_agent_system::workloads::agent_task::ExecuteMinimalCandidateBranches(
      initial_runtime, failed_session, "input:retry_task", "output:retry_task");

  store.Put("input:retry_task", "seed_mutated");
  const bool first_commit = initial_runtime.CommitTask(failed_session);
  const std::string task_log_path = "/tmp/data_agent_system_retry_loop.task.log";
  data_agent_system::runtime::WriteTaskEventLogArtifact(failed_session, task_log_path);

  std::cout << "first_commit=" << first_commit << "\n";
  std::cout << "first_reason=" << failed_session.txn.validation_result.reason << "\n";
  std::cout << "first_retry_count=" << failed_session.txn.metrics.retry_count << "\n";
  std::cout << "output_after_abort=" << store.Get("output:retry_task").value << "\n";

  TaskRuntime retry_runtime(store);
  data_agent_system::workloads::RegisterBuiltinRecoveryHandlers(retry_runtime);
  auto execution = retry_runtime.BuildRecoveryExecutionFromLog(task_log_path);
  const bool retry_committed = retry_runtime.ContinueRecoveredTask(&execution);

  std::cout << "recovery_action="
            << data_agent_system::runtime::TaskRecoveryActionName(
                   execution.recovery_plan.decision.action)
            << "\n";
  std::cout << "recovery_command="
            << data_agent_system::runtime::TaskRecoveryCommandTypeName(execution.command_type)
            << "\n";
  std::cout << "retry_committed=" << retry_committed << "\n";
  std::cout << "retry_retry_count=" << execution.session.txn.metrics.retry_count << "\n";
  std::cout << "retry_winner=" << execution.session.txn.winner_branch_id << "\n";
  std::cout << "output_after_retry=" << store.Get("output:retry_task").value << "\n";
  return 0;
}
