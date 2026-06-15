#include <iostream>

#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/runtime/task_event_log_io.h"
#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/memory_kv_store.h"
#include "data_agent_system/workloads/agent_task/minimal_candidate_task.h"

namespace {

void PrintSession(const data_agent_system::runtime::TaskSession& session) {
  std::cout << "task=" << session.task.task_id << "\n";
  std::cout << "phase=" << static_cast<int>(session.task.phase) << "\n";
  std::cout << "txn=" << session.txn.txn_id << "\n";
  std::cout << "winner=" << session.txn.winner_branch_id << "\n";
  std::cout << "validation=" << session.txn.validation_result.success << "\n";
  std::cout << "commit_log_size=" << session.txn.commit_log.entries.size() << "\n";
  std::cout << "commit_attempts=" << session.commit_attempts << "\n";
}

}  // namespace

int main() {
  using data_agent_system::cache::ObjectType;
  using data_agent_system::intent::IntentType;
  using data_agent_system::intent::WriteIntent;
  using data_agent_system::runtime::ExecutionPlan;
  using data_agent_system::runtime::TaskContext;
  using data_agent_system::runtime::TaskRuntime;
  using data_agent_system::storage::MemoryKVStore;

  MemoryKVStore store;
  store.Put("input:task_001", "seed");
  store.Put("output:task_001", "draft");

  TaskRuntime runtime(store);
  auto success_session = data_agent_system::workloads::agent_task::RunMinimalCandidateTask(
      runtime, "task_001", "input:task_001", "output:task_001");
  PrintSession(success_session);
  std::cout << "output_after_success=" << store.Get("output:task_001").value << "\n";
  data_agent_system::runtime::WriteTaskEventLogArtifact(
      success_session, "/tmp/data_agent_system_task_runtime_success.task.log");

  TaskContext task;
  task.task_id = "task_002";
  task.objective = "exercise savepoint and abort";
  task.workload_name = "task_runtime.workflow";
  task.planner_id = "workflow_demo";
  task.phase = data_agent_system::runtime::TaskPhase::kCreated;
  task.AddInputObject("input:task_001", "workflow_input");
  task.AddOutputObject("output:task_001", "workflow_output");

  ExecutionPlan plan;
  plan.AddBranchPlan("branch_X", "candidate_X", "savepoint branch", 95.0);
  plan.AddBranchPlan("branch_Y", "candidate_Y", "fallback branch", 12.0);

  auto abort_session = runtime.SubmitTask(task, plan);
  runtime.Read(abort_session, "branch_X", "input:task_001");
  runtime.Savepoint(abort_session, "branch_X", "sp1");
  runtime.Write(abort_session, "branch_X", "output:task_001", ObjectType::kText,
                WriteIntent{"output:task_001", IntentType::kAppend, "::candidate", {}});
  runtime.RollbackToSavepoint(abort_session, "branch_X", "sp1");
  runtime.Write(abort_session, "branch_X", "output:task_001", ObjectType::kText,
                WriteIntent{"output:task_001", IntentType::kOverwrite, "rolled_back_result", {}});
  runtime.StageBranch(abort_session, "branch_X", 95.0, "branch X");

  runtime.Read(abort_session, "branch_Y", "input:task_001");
  runtime.Write(abort_session, "branch_Y", "output:task_001", ObjectType::kText,
                WriteIntent{"output:task_001", IntentType::kOverwrite, "result_Y", {}});
  runtime.StageBranch(abort_session, "branch_Y", 12.0, "branch Y");

  store.Put("input:task_001", "seed_mutated");
  const bool committed = runtime.CommitTask(abort_session);
  std::cout << "conflict_commit=" << committed << "\n";
  PrintSession(abort_session);
  std::cout << "output_after_abort=" << store.Get("output:task_001").value << "\n";
  data_agent_system::runtime::WriteTaskEventLogArtifact(
      abort_session, "/tmp/data_agent_system_task_runtime_abort.task.log");
  return 0;
}
