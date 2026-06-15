#include <filesystem>
#include <iostream>
#include <string>

#include "data_agent_system/agent_txn/fallback_commit_log.h"
#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/file_kv_store.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace {

using data_agent_system::cache::ObjectType;
using data_agent_system::intent::IntentType;
using data_agent_system::intent::WriteIntent;
using data_agent_system::runtime::ExecutionPlan;
using data_agent_system::runtime::FallbackCommitRuntimeConfig;
using data_agent_system::runtime::TaskContext;
using data_agent_system::runtime::TaskRuntime;
using data_agent_system::storage::FileKVStore;
using data_agent_system::storage::VersionCheck;
using data_agent_system::storage::VersionedKVStore;
using data_agent_system::storage::VersionedValue;
using data_agent_system::storage::WriteOp;

class NonBatchFileKVStore : public VersionedKVStore {
 public:
  explicit NonBatchFileKVStore(const std::string& path) : backing_(path) {}

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
    return backing_.PutIfVersion(key, expected_version, value);
  }

  bool DeleteIfVersion(const std::string& key,
                       std::uint64_t expected_version) override {
    return backing_.DeleteIfVersion(key, expected_version);
  }

  bool BatchPutIfVersion(const std::vector<VersionCheck>&,
                         const std::vector<WriteOp>&) override {
    return false;
  }

 private:
  FileKVStore backing_;
};

TaskContext BuildTask() {
  TaskContext task;
  task.task_id = "fallback_crash_safe";
  task.objective = "crash safe fallback commit";
  task.workload_name = "demo";
  task.planner_id = "demo";
  return task;
}

ExecutionPlan BuildPlan() {
  ExecutionPlan plan;
  plan.AddBranchPlan("branch_A", "candidate_A", "crash safe fallback", 100.0);
  return plan;
}

WriteIntent Overwrite(const std::string& object_id, const std::string& value) {
  WriteIntent intent;
  intent.object_id = object_id;
  intent.intent_type = IntentType::kOverwrite;
  intent.payload = value;
  return intent;
}

WriteIntent Delta(const std::string& object_id, const std::string& value) {
  WriteIntent intent;
  intent.object_id = object_id;
  intent.intent_type = IntentType::kDelta;
  intent.payload = value;
  return intent;
}

void RunWinner(TaskRuntime& runtime,
               data_agent_system::runtime::TaskSession* session) {
  runtime.Read(*session, "branch_A", "input:crash:1");
  runtime.Write(*session, "branch_A", "summary:crash:1", ObjectType::kText,
                Overwrite("summary:crash:1", "committed_before_crash"));
  runtime.Write(*session, "branch_A", "counter:crash:1", ObjectType::kRow,
                Delta("counter:crash:1", "5"));
  runtime.StageBranch(*session, "branch_A", 100.0, "winner");
}

void ResetDir(const std::string& path) {
  std::error_code error;
  std::filesystem::remove_all(path, error);
  error.clear();
  std::filesystem::create_directories(path, error);
}

FallbackCommitRuntimeConfig BuildRuntimeConfig(const std::string& artifact_dir,
                                               const std::string& archive_dir,
                                               bool auto_recover_on_startup) {
  FallbackCommitRuntimeConfig config;
  config.artifact_dir = artifact_dir;
  config.archive_dir = archive_dir;
  config.auto_recover_on_startup = auto_recover_on_startup;
  return config;
}

}  // namespace

int main() {
  const std::string root_dir = "/tmp/das_fallback_crash_safe_demo";
  ResetDir(root_dir);

  const std::string success_store_path = root_dir + "/success_store.tsv";
  const std::string success_artifact_dir = root_dir + "/success_pending";
  const std::string success_archive_dir = root_dir + "/success_archive";
  const std::string success_artifact_path =
      success_artifact_dir + "/txn:fallback_crash_safe.fallback.log";

  {
    NonBatchFileKVStore store(success_store_path);
    store.Put("input:crash:1", "seed");
    store.Put("summary:crash:1", "draft");
    store.Put("counter:crash:1", "10");

    TaskRuntime runtime(store, BuildRuntimeConfig(success_artifact_dir, success_archive_dir, false));
    auto session = runtime.SubmitTask(BuildTask(), BuildPlan());
    RunWinner(runtime, &session);
    runtime.CommitTask(session);

    data_agent_system::agent_txn::FallbackCommitArtifact artifact;
    data_agent_system::agent_txn::ParseFallbackCommitArtifact(
        success_archive_dir + "/txn:fallback_crash_safe.fallback.log", &artifact);
    std::cout << "success_committed="
              << (session.txn.status ==
                  data_agent_system::agent_txn::TxnStatus::kCommitted)
              << "\n";
    std::cout << "success_summary=" << store.Get("summary:crash:1").value << "\n";
    std::cout << "success_counter=" << store.Get("counter:crash:1").value << "\n";
    std::cout << "success_active_artifact_exists="
              << std::filesystem::exists(success_artifact_path) << "\n";
    std::cout << "success_archived_artifact_exists="
              << std::filesystem::exists(success_archive_dir +
                                         "/txn:fallback_crash_safe.fallback.log")
              << "\n";
    std::cout << "success_archived_phase="
              << data_agent_system::agent_txn::FallbackCommitPhaseName(artifact.phase) << "\n";
  }

  const std::string crash_store_path = root_dir + "/crash_store.tsv";
  const std::string crash_artifact_dir = root_dir + "/crash_pending";
  const std::string crash_archive_dir = root_dir + "/crash_archive";
  const std::string crash_artifact_path =
      crash_artifact_dir + "/txn:fallback_crash_safe.fallback.log";

  {
    NonBatchFileKVStore store(crash_store_path);
    store.Put("input:crash:1", "seed");
    store.Put("summary:crash:1", "draft");
    store.Put("counter:crash:1", "10");

    TaskRuntime runtime(store, BuildRuntimeConfig(crash_artifact_dir, crash_archive_dir, false));
    auto session = runtime.SubmitTask(BuildTask(), BuildPlan());
    session.txn.fallback_commit.simulate_crash_after_apply_count = 1;
    RunWinner(runtime, &session);
    runtime.CommitTask(session);

    data_agent_system::agent_txn::FallbackCommitArtifact artifact;
    data_agent_system::agent_txn::ParseFallbackCommitArtifact(crash_artifact_path, &artifact);
    std::cout << "commit_after_simulated_crash="
              << (session.txn.status ==
                  data_agent_system::agent_txn::TxnStatus::kCommitted)
              << "\n";
    std::cout << "crash_reason=" << session.txn.validation_result.reason << "\n";
    std::cout << "partial_summary=" << store.Get("summary:crash:1").value << "\n";
    std::cout << "partial_counter=" << store.Get("counter:crash:1").value << "\n";
    std::cout << "artifact_phase_before_recovery="
              << data_agent_system::agent_txn::FallbackCommitPhaseName(artifact.phase) << "\n";
    std::cout << "artifact_applied_count_before_recovery=" << artifact.applied_count << "\n";
  }

  {
    NonBatchFileKVStore recovered_store(crash_store_path);
    TaskRuntime recovery_runtime(
        recovered_store,
        BuildRuntimeConfig(crash_artifact_dir, crash_archive_dir, true));
    const auto recovery = recovery_runtime.LastStartupFallbackMaintenanceResult();

    data_agent_system::agent_txn::FallbackCommitArtifact artifact;
    data_agent_system::agent_txn::ParseFallbackCommitArtifact(
        crash_archive_dir + "/txn:fallback_crash_safe.fallback.log", &artifact);
    std::cout << "recovery_success=" << (recovery.has_value() && recovery->success) << "\n";
    std::cout << "recovered_artifact_count="
              << (recovery.has_value() ? recovery->recovered_artifact_count : 0) << "\n";
    std::cout << "archived_artifact_count="
              << (recovery.has_value() ? recovery->archived_artifact_count : 0) << "\n";
    std::cout << "recovered_summary=" << recovered_store.Get("summary:crash:1").value << "\n";
    std::cout << "recovered_counter=" << recovered_store.Get("counter:crash:1").value << "\n";
    std::cout << "artifact_phase_after_recovery="
              << data_agent_system::agent_txn::FallbackCommitPhaseName(artifact.phase) << "\n";
    std::cout << "artifact_applied_count_after_recovery=" << artifact.applied_count << "\n";
    std::cout << "recovery_active_artifact_exists="
              << std::filesystem::exists(crash_artifact_path) << "\n";
    std::cout << "recovery_archived_artifact_exists="
              << std::filesystem::exists(crash_archive_dir +
                                         "/txn:fallback_crash_safe.fallback.log")
              << "\n";
  }

  return 0;
}
