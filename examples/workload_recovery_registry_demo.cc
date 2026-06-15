#include <iostream>
#include <string>

#include "data_agent_system/runtime/task_event_log_io.h"
#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/memory_kv_store.h"
#include "data_agent_system/workloads/register_builtin_recovery_handlers.h"
#include "data_agent_system/workloads/agent_task/minimal_candidate_recovery.h"
#include "data_agent_system/workloads/agent_task/minimal_candidate_task.h"
#include "data_agent_system/workloads/kv_style/scripted_kv_task.h"

int main() {
  using data_agent_system::cache::ObjectType;
  using data_agent_system::intent::IntentType;
  using data_agent_system::runtime::TaskRuntime;
  using data_agent_system::storage::MemoryKVStore;

  MemoryKVStore store;

  store.Put("input:agent_recovery", "seed_agent");
  store.Put("output:agent_recovery", "draft_agent");

  TaskRuntime producer_runtime(store);
  auto agent_session = producer_runtime.SubmitTask(
      data_agent_system::workloads::agent_task::BuildMinimalCandidateTaskContext(
          "agent_recovery", "input:agent_recovery", "output:agent_recovery"),
      data_agent_system::workloads::agent_task::BuildMinimalCandidateExecutionPlan());
  data_agent_system::workloads::agent_task::ExecuteMinimalCandidateBranches(
      producer_runtime, agent_session, "input:agent_recovery", "output:agent_recovery");
  const std::string agent_log_path = "/tmp/data_agent_system_agent_recovery.task.log";
  data_agent_system::runtime::WriteTaskEventLogArtifact(agent_session, agent_log_path);

  TaskRuntime agent_recovery_runtime(store);
  data_agent_system::workloads::RegisterBuiltinRecoveryHandlers(agent_recovery_runtime);
  data_agent_system::workloads::agent_task::RegisterMinimalCandidateContinuation(
      agent_recovery_runtime, "recovered_agent_result", 111.0, "agent recovery winner", "branch_B");
  auto agent_execution = agent_recovery_runtime.BuildRecoveryExecutionFromLog(agent_log_path);
  const bool agent_committed = agent_recovery_runtime.ContinueRecoveredTask(&agent_execution);
  std::cout << "agent_task" << '\t'
            << agent_committed << '\t'
            << agent_execution.session.txn.winner_branch_id << '\t'
            << store.Get("output:agent_recovery").value << '\n';

  store.Put("text:kv_recovery", "draft");

  data_agent_system::workloads::kv_style::KvTaskScript script;
  script.task_id = "kv_recovery";
  script.objective = "recover scripted kv workload";
  script.recovery_steps = {
      data_agent_system::workloads::kv_style::MakeReadStep("text:kv_recovery"),
      data_agent_system::workloads::kv_style::MakeWriteStep(
          "text:kv_recovery", ObjectType::kText, IntentType::kAppend, "::recovered"),
  };
  script.recovery_score = 77.0;
  script.recovery_summary = "kv recovery winner";
  script.recovery_preferred_branch_id = "branch_A";

  data_agent_system::workloads::kv_style::KvBranchProgram branch;
  branch.branch_id = "branch_A";
  branch.score = 44.0;
  branch.summary = "kv recovery branch";
  script.branches.push_back(branch);

  TaskRuntime kv_producer_runtime(store);
  auto kv_session = kv_producer_runtime.SubmitTask(
      data_agent_system::workloads::kv_style::BuildTaskContext(script),
      data_agent_system::workloads::kv_style::BuildExecutionPlan(script));
  kv_producer_runtime.Read(kv_session, "branch_A", "text:kv_recovery");
  kv_producer_runtime.Savepoint(kv_session, "branch_A", "before_edit");
  kv_producer_runtime.Write(
      kv_session, "branch_A", "text:kv_recovery", ObjectType::kText,
      data_agent_system::workloads::kv_style::MakeWriteStep("text:kv_recovery", ObjectType::kText,
                                                            IntentType::kAppend, "::draft")
          .intent);
  const std::string kv_log_path = "/tmp/data_agent_system_kv_recovery.task.log";
  data_agent_system::runtime::WriteTaskEventLogArtifact(kv_session, kv_log_path);

  TaskRuntime kv_recovery_runtime(store);
  data_agent_system::workloads::RegisterBuiltinRecoveryHandlers(kv_recovery_runtime);
  auto kv_execution = kv_recovery_runtime.BuildRecoveryExecutionFromLog(kv_log_path);
  const bool kv_committed = kv_recovery_runtime.ContinueRecoveredTask(&kv_execution);
  std::cout << "kv_style" << '\t'
            << kv_committed << '\t'
            << kv_execution.session.txn.winner_branch_id << '\t'
            << store.Get("text:kv_recovery").value << '\n';

  return 0;
}
