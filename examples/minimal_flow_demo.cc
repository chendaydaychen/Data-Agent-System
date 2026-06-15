#include <iostream>

#include "data_agent_system/agent_txn/agent_txn_manager.h"
#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/storage/memory_kv_store.h"

int main() {
  using data_agent_system::agent_txn::AgentTxnManager;
  using data_agent_system::cache::ObjectCacheEntry;
  using data_agent_system::cache::ObjectType;
  using data_agent_system::intent::IntentType;
  using data_agent_system::intent::WriteIntent;
  using data_agent_system::storage::MemoryKVStore;

  MemoryKVStore store;
  store.Put("input:task_001", "seed");
  store.Put("output:task_001", "draft");

  auto make_entry = [](const std::string& value) {
    ObjectCacheEntry entry;
    entry.object_id = "output:task_001";
    entry.object_type = ObjectType::kText;
    entry.base_value = "draft";
    entry.base_version = 1;
    entry.current_value = value;
    entry.dirty = true;
    entry.intent_type = IntentType::kOverwrite;
    entry.undo_record.previous_value = "draft";
    entry.undo_record.existed = true;
    return entry;
  };

  auto make_intent = [](const std::string& value) {
    WriteIntent intent;
    intent.object_id = "output:task_001";
    intent.intent_type = IntentType::kOverwrite;
    intent.payload = value;
    return intent;
  };

  AgentTxnManager txn_manager;
  auto txn = txn_manager.Begin("txn_001", "task_001");

  auto& branch_a = txn_manager.CreateBranch(txn, "branch_A");
  branch_a.status = data_agent_system::branch::BranchStatus::kRunning;
  branch_a.read_set.Record("input:task_001", store.GetVersion("input:task_001"));
  branch_a.write_buffer.Upsert(make_entry("result_A"));
  branch_a.intent_log.Append(make_intent("result_A"));
  branch_a.candidate_result.score = 72.0;

  auto& branch_b = txn_manager.CreateBranch(txn, "branch_B");
  branch_b.status = data_agent_system::branch::BranchStatus::kRunning;
  branch_b.read_set.Record("input:task_001", store.GetVersion("input:task_001"));
  branch_b.write_buffer.Upsert(make_entry("result_B"));
  branch_b.intent_log.Append(make_intent("result_B"));
  branch_b.candidate_result.score = 91.0;

  auto& branch_c = txn_manager.CreateBranch(txn, "branch_C");
  branch_c.status = data_agent_system::branch::BranchStatus::kRunning;
  branch_c.read_set.Record("input:task_001", store.GetVersion("input:task_001"));
  branch_c.write_buffer.Upsert(make_entry("result_C"));
  branch_c.intent_log.Append(make_intent("result_C"));
  branch_c.candidate_result.score = 65.0;

  const bool committed = txn_manager.SelectWinnerAndCommit(txn, store);
  std::cout << "committed=" << committed << "\n";
  std::cout << "winner=" << txn.winner_branch_id << "\n";
  std::cout << "output=" << store.Get("output:task_001").value << "\n";
  std::cout << "output_version=" << store.GetVersion("output:task_001") << "\n";
  return committed ? 0 : 1;
}
