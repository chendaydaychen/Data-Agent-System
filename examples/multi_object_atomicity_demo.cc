#include <iostream>
#include <string>

#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/memory_kv_store.h"
#include "data_agent_system/workloads/kv_style/scripted_kv_task.h"

namespace {

using data_agent_system::cache::ObjectType;
using data_agent_system::intent::Condition;
using data_agent_system::intent::ConditionType;
using data_agent_system::intent::IntentType;
using data_agent_system::runtime::TaskRuntime;
using data_agent_system::storage::MemoryKVStore;
using data_agent_system::workloads::kv_style::KvBranchProgram;
using data_agent_system::workloads::kv_style::KvTaskScript;
using data_agent_system::workloads::kv_style::MakeReadStep;
using data_agent_system::workloads::kv_style::MakeWriteStep;
using data_agent_system::workloads::kv_style::RunScriptedKvTask;

KvTaskScript BuildSuccessScript() {
  KvTaskScript script;
  script.task_id = "multi_object_success";
  script.objective = "commit one winner across multiple objects";

  KvBranchProgram branch_a;
  branch_a.branch_id = "branch_A";
  branch_a.score = 10.0;
  branch_a.summary = "fallback branch";
  branch_a.steps = {
      MakeReadStep("input:order:1"),
      MakeReadStep("input:user:1"),
      MakeWriteStep("text:summary:1", ObjectType::kText, IntentType::kOverwrite,
                    "fallback_summary"),
  };
  script.branches.push_back(branch_a);

  KvBranchProgram branch_b;
  branch_b.branch_id = "branch_B";
  branch_b.score = 99.0;
  branch_b.summary = "winner branch";
  branch_b.steps = {
      MakeReadStep("input:order:1"),
      MakeReadStep("input:user:1"),
      MakeWriteStep("text:summary:1", ObjectType::kText, IntentType::kOverwrite,
                    "winner_summary"),
      MakeWriteStep("row:inventory:1", ObjectType::kRow, IntentType::kDelta, "-2"),
      MakeWriteStep("state:task:1", ObjectType::kGeneric, IntentType::kCas, "confirmed",
                    Condition{ConditionType::kValueEquals, "pending"}),
  };
  script.branches.push_back(branch_b);
  return script;
}

void SeedStoreForSuccess(MemoryKVStore* store) {
  store->Put("input:order:1", "order_seed");
  store->Put("input:user:1", "user_seed");
  store->Put("text:summary:1", "draft");
  store->Put("row:inventory:1", "10");
  store->Put("state:task:1", "pending");
}

void SeedStoreForConflict(MemoryKVStore* store) {
  store->Put("input:order:2", "order_seed");
  store->Put("input:user:2", "user_seed");
  store->Put("text:summary:2", "draft_conflict");
  store->Put("row:inventory:2", "20");
  store->Put("state:task:2", "pending");
}

}  // namespace

int main() {
  MemoryKVStore store;
  SeedStoreForSuccess(&store);

  TaskRuntime runtime(store);
  auto success_session = RunScriptedKvTask(runtime, BuildSuccessScript());
  std::cout << "success_committed="
            << (success_session.txn.status == data_agent_system::agent_txn::TxnStatus::kCommitted)
            << "\n";
  std::cout << "success_winner=" << success_session.txn.winner_branch_id << "\n";
  std::cout << "success_summary=" << store.Get("text:summary:1").value << "\n";
  std::cout << "success_inventory=" << store.Get("row:inventory:1").value << "\n";
  std::cout << "success_state=" << store.Get("state:task:1").value << "\n";
  std::cout << "success_commit_log_size=" << success_session.txn.commit_log.entries.size() << "\n";
  std::cout << "success_output_objects=" << success_session.task.output_objects.size() << "\n";

  SeedStoreForConflict(&store);

  KvTaskScript conflict_script;
  conflict_script.task_id = "multi_object_conflict";
  conflict_script.objective = "abort one winner across multiple objects";

  KvBranchProgram conflict_branch;
  conflict_branch.branch_id = "branch_conflict";
  conflict_branch.score = 88.0;
  conflict_branch.summary = "conflicting branch";
  conflict_branch.steps = {
      MakeReadStep("input:order:2"),
      MakeReadStep("input:user:2"),
      MakeWriteStep("text:summary:2", ObjectType::kText, IntentType::kOverwrite,
                    "should_not_commit"),
      MakeWriteStep("row:inventory:2", ObjectType::kRow, IntentType::kDelta, "-5"),
      MakeWriteStep("state:task:2", ObjectType::kGeneric, IntentType::kCas, "confirmed",
                    Condition{ConditionType::kValueEquals, "pending"}),
  };
  conflict_script.branches.push_back(conflict_branch);

  auto conflict_session = runtime.SubmitTask(
      data_agent_system::workloads::kv_style::BuildTaskContext(conflict_script),
      data_agent_system::workloads::kv_style::BuildExecutionPlan(conflict_script));
  for (const auto& step : conflict_branch.steps) {
    if (step.kind == data_agent_system::workloads::kv_style::StepKind::kRead) {
      runtime.Read(conflict_session, conflict_branch.branch_id, step.object_id);
    } else if (step.kind == data_agent_system::workloads::kv_style::StepKind::kWrite) {
      runtime.Write(conflict_session, conflict_branch.branch_id, step.object_id, step.object_type,
                    step.intent);
    }
  }
  runtime.StageBranch(conflict_session, conflict_branch.branch_id, conflict_branch.score,
                      conflict_branch.summary);

  store.Put("input:order:2", "order_seed_mutated");
  const bool conflict_committed = runtime.CommitTask(conflict_session);
  std::cout << "conflict_committed=" << conflict_committed << "\n";
  std::cout << "conflict_reason=" << conflict_session.txn.validation_result.reason << "\n";
  std::cout << "conflict_summary=" << store.Get("text:summary:2").value << "\n";
  std::cout << "conflict_inventory=" << store.Get("row:inventory:2").value << "\n";
  std::cout << "conflict_state=" << store.Get("state:task:2").value << "\n";
  std::cout << "conflict_commit_log_size=" << conflict_session.txn.commit_log.entries.size()
            << "\n";
  std::cout << "conflict_output_objects=" << conflict_session.task.output_objects.size() << "\n";
  return 0;
}
