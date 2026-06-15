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

KvTaskScript BuildSingleBranchScript(const std::string& task_id,
                                     const std::string& objective,
                                     const std::vector<data_agent_system::workloads::kv_style::KvStep>& steps) {
  KvTaskScript script;
  script.task_id = task_id;
  script.objective = objective;
  KvBranchProgram branch;
  branch.branch_id = "branch_semantic";
  branch.score = 100.0;
  branch.summary = "semantic winner";
  branch.steps = steps;
  script.branches.push_back(branch);
  return script;
}

template <typename Mutator>
data_agent_system::runtime::TaskSession RunMutatedCommit(TaskRuntime& runtime,
                                                         const KvTaskScript& script,
                                                         const std::string& branch_id,
                                                         Mutator mutate_store) {
  auto session = runtime.SubmitTask(
      data_agent_system::workloads::kv_style::BuildTaskContext(script),
      data_agent_system::workloads::kv_style::BuildExecutionPlan(script));
  for (const auto& step : script.branches.front().steps) {
    if (step.kind == data_agent_system::workloads::kv_style::StepKind::kRead) {
      runtime.Read(session, branch_id, step.object_id);
    } else if (step.kind == data_agent_system::workloads::kv_style::StepKind::kWrite) {
      runtime.Write(session, branch_id, step.object_id, step.object_type, step.intent);
    }
  }
  runtime.StageBranch(session, branch_id, script.branches.front().score,
                      script.branches.front().summary);
  mutate_store();
  runtime.CommitTask(session);
  return session;
}

}  // namespace

int main() {
  MemoryKVStore store;
  TaskRuntime runtime(store);

  store.Put("text:append:1", "draft");
  auto append_session = RunMutatedCommit(
      runtime,
      BuildSingleBranchScript(
          "semantic_append", "append after concurrent update",
          {MakeReadStep("text:append:1"),
           MakeWriteStep("text:append:1", ObjectType::kText, IntentType::kAppend, "::agent")}),
      "branch_semantic",
      [&]() { store.Put("text:append:1", "draft::external"); });
  std::cout << "append_committed="
            << (append_session.txn.status == data_agent_system::agent_txn::TxnStatus::kCommitted)
            << "\n";
  std::cout << "append_value=" << store.Get("text:append:1").value << "\n";
  std::cout << "append_reason=" << append_session.txn.validation_result.reason << "\n";

  store.Put("counter:delta:1", "10");
  auto delta_session = RunMutatedCommit(
      runtime,
      BuildSingleBranchScript(
          "semantic_delta", "delta after concurrent update",
          {MakeReadStep("counter:delta:1"),
           MakeWriteStep("counter:delta:1", ObjectType::kRow, IntentType::kDelta, "5")}),
      "branch_semantic",
      [&]() { store.Put("counter:delta:1", "12"); });
  std::cout << "delta_committed="
            << (delta_session.txn.status == data_agent_system::agent_txn::TxnStatus::kCommitted)
            << "\n";
  std::cout << "delta_value=" << store.Get("counter:delta:1").value << "\n";
  std::cout << "delta_reason=" << delta_session.txn.validation_result.reason << "\n";

  store.Put("state:cas:1", "pending");
  auto cas_success_session = RunMutatedCommit(
      runtime,
      BuildSingleBranchScript(
          "semantic_cas_success", "cas success after unrelated refresh",
          {MakeReadStep("state:cas:1"),
           MakeWriteStep("state:cas:1", ObjectType::kGeneric, IntentType::kCas, "confirmed",
                         Condition{ConditionType::kValueEquals, "pending"})}),
      "branch_semantic",
      [&]() { store.Put("state:cas:1", "pending"); });
  std::cout << "cas_success_committed="
            << (cas_success_session.txn.status == data_agent_system::agent_txn::TxnStatus::kCommitted)
            << "\n";
  std::cout << "cas_success_value=" << store.Get("state:cas:1").value << "\n";
  std::cout << "cas_success_reason=" << cas_success_session.txn.validation_result.reason << "\n";

  store.Put("state:cas:2", "pending");
  auto cas_fail_session = RunMutatedCommit(
      runtime,
      BuildSingleBranchScript(
          "semantic_cas_fail", "cas fail when condition no longer holds",
          {MakeReadStep("state:cas:2"),
           MakeWriteStep("state:cas:2", ObjectType::kGeneric, IntentType::kCas, "confirmed",
                         Condition{ConditionType::kValueEquals, "pending"})}),
      "branch_semantic",
      [&]() { store.Put("state:cas:2", "cancelled"); });
  std::cout << "cas_fail_committed="
            << (cas_fail_session.txn.status == data_agent_system::agent_txn::TxnStatus::kCommitted)
            << "\n";
  std::cout << "cas_fail_value=" << store.Get("state:cas:2").value << "\n";
  std::cout << "cas_fail_reason=" << cas_fail_session.txn.validation_result.reason << "\n";
  return 0;
}
