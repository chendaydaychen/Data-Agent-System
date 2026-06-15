#include <iostream>

#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/runtime/task_event_log_io.h"
#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/memory_kv_store.h"
#include "data_agent_system/workloads/kv_style/scripted_kv_task.h"

int main() {
  using data_agent_system::cache::ObjectType;
  using data_agent_system::intent::Condition;
  using data_agent_system::intent::ConditionType;
  using data_agent_system::intent::IntentType;
  using data_agent_system::runtime::TaskRuntime;
  using data_agent_system::storage::MemoryKVStore;
  using data_agent_system::workloads::kv_style::KvBranchProgram;
  using data_agent_system::workloads::kv_style::KvTaskScript;
  using data_agent_system::workloads::kv_style::MakeReadStep;
  using data_agent_system::workloads::kv_style::MakeRollbackStep;
  using data_agent_system::workloads::kv_style::MakeSavepointStep;
  using data_agent_system::workloads::kv_style::MakeWriteStep;
  using data_agent_system::workloads::kv_style::RunScriptedKvTask;

  MemoryKVStore store;
  store.Put("text:summary:task_001", "draft");
  store.Put("row:inventory:item_8", "10");
  store.Put("state:task_001", "pending");

  TaskRuntime runtime(store);

  KvTaskScript script;
  script.task_id = "kv_task_001";
  script.objective = "exercise scripted versioned-KV workflow";

  KvBranchProgram branch_a;
  branch_a.branch_id = "branch_A";
  branch_a.score = 55.0;
  branch_a.summary = "lower-score fallback";
  branch_a.steps = {
      MakeReadStep("text:summary:task_001"),
      MakeWriteStep("text:summary:task_001", ObjectType::kText, IntentType::kOverwrite,
                    "fallback_summary"),
  };
  script.branches.push_back(branch_a);

  KvBranchProgram branch_b;
  branch_b.branch_id = "branch_B";
  branch_b.score = 95.0;
  branch_b.summary = "winner with append, delta, cas, and rollback";
  branch_b.steps = {
      MakeReadStep("text:summary:task_001"),
      MakeWriteStep("text:summary:task_001", ObjectType::kText, IntentType::kAppend, "::draft"),
      MakeSavepointStep("summary_sp"),
      MakeWriteStep("text:summary:task_001", ObjectType::kText, IntentType::kAppend, "::discard"),
      MakeRollbackStep("summary_sp"),
      MakeWriteStep("text:summary:task_001", ObjectType::kText, IntentType::kAppend, "::winner"),
      MakeReadStep("row:inventory:item_8"),
      MakeWriteStep("row:inventory:item_8", ObjectType::kRow, IntentType::kDelta, "2"),
      MakeSavepointStep("inventory_sp"),
      MakeWriteStep("row:inventory:item_8", ObjectType::kRow, IntentType::kDelta, "7"),
      MakeRollbackStep("inventory_sp"),
      MakeWriteStep("row:inventory:item_8", ObjectType::kRow, IntentType::kDelta, "3"),
      MakeReadStep("state:task_001"),
      MakeWriteStep("state:task_001", ObjectType::kGeneric, IntentType::kCas, "confirmed",
                    Condition{ConditionType::kValueEquals, "pending"}),
  };
  script.branches.push_back(branch_b);

  auto session = RunScriptedKvTask(runtime, script);

  std::cout << "committed="
            << (session.txn.status == data_agent_system::agent_txn::TxnStatus::kCommitted)
            << "\n";
  std::cout << "task_phase=" << static_cast<int>(session.task.phase) << "\n";
  std::cout << "winner=" << session.txn.winner_branch_id << "\n";
  std::cout << "summary=" << store.Get("text:summary:task_001").value << "\n";
  std::cout << "inventory=" << store.Get("row:inventory:item_8").value << "\n";
  std::cout << "state=" << store.Get("state:task_001").value << "\n";
  std::cout << "commit_log_size=" << session.txn.commit_log.entries.size() << "\n";
  std::cout << "commit_attempts=" << session.commit_attempts << "\n";
  data_agent_system::runtime::WriteTaskEventLogArtifact(
      session, "/tmp/data_agent_system_kv_style_workflow.task.log");
  return session.txn.status == data_agent_system::agent_txn::TxnStatus::kCommitted ? 0 : 1;
}
