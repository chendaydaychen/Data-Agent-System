#include <iostream>
#include <string>

#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/intent/policy_dispatcher.h"
#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/memory_kv_store.h"

namespace {

using data_agent_system::cache::ObjectType;
using data_agent_system::intent::IntentType;
using data_agent_system::intent::PolicyDispatcher;
using data_agent_system::intent::WriteIntent;
using data_agent_system::runtime::ExecutionPlan;
using data_agent_system::runtime::TaskContext;
using data_agent_system::runtime::TaskRuntime;
using data_agent_system::storage::MemoryKVStore;

TaskContext BuildTask(const std::string& task_id) {
  TaskContext task;
  task.task_id = task_id;
  task.objective = "semantic contention benchmark";
  task.workload_name = "semantic_contention_demo";
  task.planner_id = "demo";
  return task;
}

ExecutionPlan BuildPlan() {
  ExecutionPlan plan;
  plan.AddBranchPlan("branch_A", "candidate_A", "contention branch", 100.0);
  return plan;
}

int RunOverwriteRounds(TaskRuntime& runtime, MemoryKVStore& store, int rounds) {
  int committed = 0;
  for (int i = 0; i < rounds; ++i) {
    auto session = runtime.SubmitTask(BuildTask("overwrite_" + std::to_string(i)), BuildPlan());
    const auto before = runtime.Read(session, "branch_A", "counter:overwrite");
    WriteIntent intent;
    intent.object_id = "counter:overwrite";
    intent.intent_type = IntentType::kOverwrite;
    intent.payload = std::to_string(std::stoll(before.value) + 1);
    runtime.Write(session, "branch_A", "counter:overwrite", ObjectType::kRow, intent);
    runtime.StageBranch(session, "branch_A", 100.0, "overwrite");

    const auto external = store.Get("counter:overwrite");
    store.Put("counter:overwrite", std::to_string(std::stoll(external.value) + 1));
    committed += runtime.CommitTask(session) ? 1 : 0;
  }
  return committed;
}

int RunDeltaRounds(TaskRuntime& runtime, MemoryKVStore& store, int rounds) {
  int committed = 0;
  for (int i = 0; i < rounds; ++i) {
    auto session = runtime.SubmitTask(BuildTask("delta_" + std::to_string(i)), BuildPlan());
    runtime.Read(session, "branch_A", "counter:delta");
    WriteIntent intent;
    intent.object_id = "counter:delta";
    intent.intent_type = IntentType::kDelta;
    intent.payload = "1";
    runtime.Write(session, "branch_A", "counter:delta", ObjectType::kRow, intent);
    runtime.StageBranch(session, "branch_A", 100.0, "delta");

    const auto external = store.Get("counter:delta");
    store.Put("counter:delta", std::to_string(std::stoll(external.value) + 1));
    committed += runtime.CommitTask(session) ? 1 : 0;
  }
  return committed;
}

}  // namespace

int main() {
  constexpr int kRounds = 8;

  MemoryKVStore store;
  TaskRuntime runtime(store);
  store.Put("counter:overwrite", "0");
  store.Put("counter:delta", "0");

  const int overwrite_committed = RunOverwriteRounds(runtime, store, kRounds);
  const int delta_committed = RunDeltaRounds(runtime, store, kRounds);

  std::cout << "rounds=" << kRounds << "\n";
  std::cout << "overwrite_rule="
            << PolicyDispatcher::ConcurrencyClassName(IntentType::kOverwrite) << "\n";
  std::cout << "overwrite_committed=" << overwrite_committed << "\n";
  std::cout << "overwrite_aborted=" << (kRounds - overwrite_committed) << "\n";
  std::cout << "overwrite_final_value=" << store.Get("counter:overwrite").value << "\n";
  std::cout << "delta_rule=" << PolicyDispatcher::ConcurrencyClassName(IntentType::kDelta) << "\n";
  std::cout << "delta_committed=" << delta_committed << "\n";
  std::cout << "delta_aborted=" << (kRounds - delta_committed) << "\n";
  std::cout << "delta_final_value=" << store.Get("counter:delta").value << "\n";
  return 0;
}
