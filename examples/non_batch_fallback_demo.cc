#include <iostream>
#include <string>

#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/memory_kv_store.h"
#include "data_agent_system/storage/non_batch_memory_kv_store.h"
#include "data_agent_system/workloads/kv_style/scripted_kv_task.h"

namespace {

using data_agent_system::cache::ObjectType;
using data_agent_system::intent::IntentType;
using data_agent_system::runtime::TaskRuntime;
using data_agent_system::storage::MemoryKVStore;
using data_agent_system::storage::NonBatchMemoryKVStore;
using data_agent_system::storage::VersionCheck;
using data_agent_system::storage::VersionedKVStore;
using data_agent_system::storage::VersionedValue;
using data_agent_system::storage::WriteOp;
using data_agent_system::workloads::kv_style::KvBranchProgram;
using data_agent_system::workloads::kv_style::KvTaskScript;
using data_agent_system::workloads::kv_style::MakeReadStep;
using data_agent_system::workloads::kv_style::MakeWriteStep;
using data_agent_system::workloads::kv_style::RunScriptedKvTask;

class HookedNonBatchMemoryKVStore : public VersionedKVStore {
 public:
  bool SupportsAtomicBatchConditionalWrite() const override { return false; }

  VersionedValue Get(const std::string& key) const override { return backing_.Get(key); }

  std::uint64_t GetVersion(const std::string& key) const override {
    return backing_.GetVersion(key);
  }

  bool Put(const std::string& key, const std::string& value) override {
    return backing_.Put(key, value);
  }

  bool PutIfVersion(const std::string& key,
                    std::uint64_t expected_version,
                    const std::string& value) override {
    const bool written = backing_.PutIfVersion(key, expected_version, value);
    if (written && hook_enabled_) {
      successful_put_if_version_count_ += 1;
      if (successful_put_if_version_count_ == trigger_after_success_count_) {
        backing_.Put(hook_key_, hook_value_);
      }
    }
    return written;
  }

  bool DeleteIfVersion(const std::string& key,
                       std::uint64_t expected_version) override {
    return backing_.DeleteIfVersion(key, expected_version);
  }

  bool BatchPutIfVersion(const std::vector<VersionCheck>&,
                         const std::vector<WriteOp>&) override {
    return false;
  }

  void ConfigureConflictHook(std::size_t trigger_after_success_count,
                             const std::string& hook_key,
                             const std::string& hook_value) {
    hook_enabled_ = true;
    trigger_after_success_count_ = trigger_after_success_count;
    hook_key_ = hook_key;
    hook_value_ = hook_value;
    successful_put_if_version_count_ = 0;
  }

 private:
  MemoryKVStore backing_;
  bool hook_enabled_ = false;
  std::size_t trigger_after_success_count_ = 0;
  std::size_t successful_put_if_version_count_ = 0;
  std::string hook_key_;
  std::string hook_value_;
};

KvTaskScript BuildSuccessScript() {
  KvTaskScript script;
  script.task_id = "non_batch_success";
  script.objective = "commit through fallback conditional writes";

  KvBranchProgram branch;
  branch.branch_id = "branch_A";
  branch.score = 90.0;
  branch.summary = "fallback winner";
  branch.steps = {
      MakeReadStep("input:fallback:1"),
      MakeWriteStep("text:fallback:1", ObjectType::kText, IntentType::kOverwrite,
                    "fallback_result"),
      MakeWriteStep("counter:fallback:1", ObjectType::kRow, IntentType::kDelta, "3"),
  };
  script.branches.push_back(branch);
  return script;
}

KvTaskScript BuildConflictScript() {
  KvTaskScript script;
  script.task_id = "non_batch_conflict";
  script.objective = "rollback partial fallback writes after conflict";

  KvBranchProgram branch;
  branch.branch_id = "branch_conflict";
  branch.score = 90.0;
  branch.summary = "fallback conflict winner";
  branch.steps = {
      MakeReadStep("input:fallback:2"),
      MakeWriteStep("text:fallback:2", ObjectType::kText, IntentType::kOverwrite,
                    "should_rollback"),
      MakeWriteStep("counter:fallback:2", ObjectType::kRow, IntentType::kDelta, "5"),
  };
  script.branches.push_back(branch);
  return script;
}

KvTaskScript BuildCreateThenRollbackScript() {
  KvTaskScript script;
  script.task_id = "non_batch_create_conflict";
  script.objective = "rollback newly created key after fallback conflict";

  KvBranchProgram branch;
  branch.branch_id = "branch_create_conflict";
  branch.score = 90.0;
  branch.summary = "fallback create conflict winner";
  branch.steps = {
      MakeReadStep("input:fallback:3"),
      MakeWriteStep("new:fallback:3", ObjectType::kText, IntentType::kOverwrite, "created"),
      MakeWriteStep("counter:fallback:3", ObjectType::kRow, IntentType::kDelta, "2"),
  };
  script.branches.push_back(branch);
  return script;
}

}  // namespace

int main() {
  NonBatchMemoryKVStore success_store;
  success_store.Put("input:fallback:1", "seed");
  success_store.Put("text:fallback:1", "draft");
  success_store.Put("counter:fallback:1", "10");

  TaskRuntime success_runtime(success_store);
  auto success_session = RunScriptedKvTask(success_runtime, BuildSuccessScript());
  std::cout << "success_committed="
            << (success_session.txn.status == data_agent_system::agent_txn::TxnStatus::kCommitted)
            << "\n";
  std::cout << "success_summary=" << success_store.Get("text:fallback:1").value << "\n";
  std::cout << "success_counter=" << success_store.Get("counter:fallback:1").value << "\n";
  std::cout << "success_commit_log_size=" << success_session.txn.commit_log.entries.size() << "\n";
  std::cout << "success_store_supports_batch="
            << success_store.SupportsAtomicBatchConditionalWrite() << "\n";

  HookedNonBatchMemoryKVStore conflict_store;
  conflict_store.Put("input:fallback:2", "seed");
  conflict_store.Put("text:fallback:2", "draft_conflict");
  conflict_store.Put("counter:fallback:2", "20");
  conflict_store.ConfigureConflictHook(1, "counter:fallback:2", "20");

  TaskRuntime conflict_runtime(conflict_store);
  auto conflict_session = RunScriptedKvTask(conflict_runtime, BuildConflictScript());
  std::cout << "conflict_committed="
            << (conflict_session.txn.status == data_agent_system::agent_txn::TxnStatus::kCommitted)
            << "\n";
  std::cout << "conflict_reason=" << conflict_session.txn.validation_result.reason << "\n";
  std::cout << "conflict_summary=" << conflict_store.Get("text:fallback:2").value << "\n";
  std::cout << "conflict_counter=" << conflict_store.Get("counter:fallback:2").value << "\n";
  std::cout << "conflict_commit_log_size=" << conflict_session.txn.commit_log.entries.size()
            << "\n";
  std::cout << "conflict_store_supports_batch="
            << conflict_store.SupportsAtomicBatchConditionalWrite() << "\n";

  HookedNonBatchMemoryKVStore create_conflict_store;
  create_conflict_store.Put("input:fallback:3", "seed");
  create_conflict_store.Put("counter:fallback:3", "30");
  create_conflict_store.ConfigureConflictHook(1, "counter:fallback:3", "30");

  TaskRuntime create_conflict_runtime(create_conflict_store);
  auto create_conflict_session =
      RunScriptedKvTask(create_conflict_runtime, BuildCreateThenRollbackScript());
  std::cout << "create_conflict_committed="
            << (create_conflict_session.txn.status ==
                data_agent_system::agent_txn::TxnStatus::kCommitted)
            << "\n";
  std::cout << "create_conflict_reason=" << create_conflict_session.txn.validation_result.reason
            << "\n";
  std::cout << "create_conflict_new_key_exists="
            << create_conflict_store.Get("new:fallback:3").exists << "\n";
  std::cout << "create_conflict_counter=" << create_conflict_store.Get("counter:fallback:3").value
            << "\n";
  return 0;
}
