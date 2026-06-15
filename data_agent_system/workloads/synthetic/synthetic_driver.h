#pragma once

#include <chrono>
#include <cstddef>
#include <string>
#include <ostream>
#include <vector>

#include "data_agent_system/agent_txn/commit_log_io.h"
#include "data_agent_system/agent_txn/agent_txn_context.h"
#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/runtime/execution_plan.h"
#include "data_agent_system/runtime/task_event_log_io.h"
#include "data_agent_system/runtime/task_context.h"
#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/memory_kv_store.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::workloads::synthetic {

struct SyntheticRunConfig {
  std::size_t task_count = 10;
  std::size_t branch_count = 3;
  std::size_t conflict_every = 3;
  std::string commit_log_dir;
  std::string task_event_log_dir;
};

struct SyntheticTaskRecord {
  std::string task_id;
  bool committed = false;
  std::size_t branch_count = 0;
  std::size_t planned_loser_count = 0;
  std::size_t winner_commit_count = 0;
  std::size_t real_abort_count = 0;
  std::size_t conflict_abort_count = 0;
  std::size_t validation_fail_count = 0;
  std::size_t retry_count = 0;
  double commit_latency_us = 0.0;
  std::string commit_log_path;
  std::size_t commit_log_entry_count = 0;
  std::string task_event_log_path;
  std::size_t task_event_count = 0;
};

struct SyntheticRunResult {
  std::size_t task_count = 0;
  std::size_t committed_task_count = 0;
  std::size_t aborted_task_count = 0;
  std::vector<SyntheticTaskRecord> tasks;
};

inline void AppendTaskRecord(SyntheticRunResult* result, const SyntheticTaskRecord& record) {
  if (result == nullptr) {
    return;
  }
  if (record.committed) {
    result->committed_task_count += 1;
  } else {
    result->aborted_task_count += 1;
  }
  result->tasks.push_back(record);
}

inline SyntheticTaskRecord RunOneTask(
    std::size_t task_index,
    const SyntheticRunConfig& config,
    data_agent_system::runtime::TaskRuntime& runtime,
    data_agent_system::storage::VersionedKVStore& store) {
  using data_agent_system::cache::ObjectType;
  using data_agent_system::intent::IntentType;
  using data_agent_system::intent::WriteIntent;

  const std::string task_id = "task_" + std::to_string(task_index);
  const std::string input_key = "input:" + task_id;
  const std::string output_key = "output:" + task_id;

  store.Put(input_key, "seed_" + std::to_string(task_index));
  store.Put(output_key, "draft_" + std::to_string(task_index));

  data_agent_system::runtime::TaskContext task;
  task.task_id = task_id;
  task.objective = "synthetic candidate-selection workload";
  task.workload_name = "synthetic.candidate_selection";
  task.planner_id = "synthetic_planner";
  task.phase = data_agent_system::runtime::TaskPhase::kCreated;
  task.AddInputObject(input_key, "synthetic_input");
  task.AddOutputObject(output_key, "synthetic_output");
  task.SetMetadata("task_index", std::to_string(task_index));
  task.SetMetadata("branch_count", std::to_string(config.branch_count));

  data_agent_system::runtime::ExecutionPlan plan;
  for (std::size_t i = 0; i < config.branch_count; ++i) {
    const auto branch_id = "branch_" + std::to_string(i);
    plan.AddBranchPlan(branch_id, "candidate_" + std::to_string(i),
                       "synthetic candidate " + std::to_string(i), static_cast<double>(i));
  }

  auto session = runtime.SubmitTask(task, plan);
  const auto branch_ids = plan.CandidateBranchIds();
  session.txn.metrics.branch_count = branch_ids.size();
  session.txn.metrics.planned_loser_count =
      branch_ids.empty() ? 0 : branch_ids.size() - 1;

  for (std::size_t i = 0; i < branch_ids.size(); ++i) {
    const std::string& branch_id = branch_ids[i];
    runtime.Read(session, branch_id, input_key);
    if (i == 0) {
      runtime.Savepoint(session, branch_id, "before_write");
      runtime.Write(session, branch_id, output_key, ObjectType::kText,
                    WriteIntent{output_key, IntentType::kAppend, "::tentative", {}});
      runtime.RollbackToSavepoint(session, branch_id, "before_write");
    }

    runtime.Write(session, branch_id, output_key, ObjectType::kText,
                  WriteIntent{
                      output_key,
                      IntentType::kOverwrite,
                      "result_" + std::to_string(task_index) + "_" + std::to_string(i),
                      {},
                  });
    runtime.StageBranch(session, branch_id, static_cast<double>((i + 1) * 10 + task_index),
                        "candidate_" + std::to_string(i));
  }

  const bool inject_conflict =
      (config.conflict_every != 0) && ((task_index + 1) % config.conflict_every == 0);
  if (inject_conflict) {
    store.Put(input_key, "mutated_" + std::to_string(task_index));
  }

  const auto start = std::chrono::steady_clock::now();
  const bool committed = runtime.CommitTask(session);
  const auto end = std::chrono::steady_clock::now();
  session.txn.metrics.commit_latency_us =
      std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
  session.txn.metrics.winner_commit_count = committed ? 1 : 0;
  session.txn.metrics.real_abort_count = committed ? 0 : 1;
  session.txn.metrics.conflict_abort_count =
      (!committed && inject_conflict && !session.txn.validation_result.success) ? 1 : 0;
  session.txn.metrics.validation_fail_count = (!session.txn.validation_result.success) ? 1 : 0;

  SyntheticTaskRecord record;
  record.task_id = task_id;
  record.committed = committed;
  record.branch_count = session.txn.metrics.branch_count;
  record.planned_loser_count = session.txn.metrics.planned_loser_count;
  record.winner_commit_count = session.txn.metrics.winner_commit_count;
  record.real_abort_count = session.txn.metrics.real_abort_count;
  record.conflict_abort_count = session.txn.metrics.conflict_abort_count;
  record.validation_fail_count = session.txn.metrics.validation_fail_count;
  record.retry_count = session.txn.metrics.retry_count;
  record.commit_latency_us = session.txn.metrics.commit_latency_us;
  record.commit_log_entry_count = session.txn.commit_log.entries.size();
  record.task_event_count = session.events.size();
  if (!config.commit_log_dir.empty()) {
    record.commit_log_path = config.commit_log_dir + "/" + task_id + ".commit.log";
    data_agent_system::agent_txn::WriteCommitLogArtifact(session.txn, record.commit_log_path);
  }
  if (!config.task_event_log_dir.empty()) {
    record.task_event_log_path = config.task_event_log_dir + "/" + task_id + ".task.log";
    data_agent_system::runtime::WriteTaskEventLogArtifact(session, record.task_event_log_path);
  }
  return record;
}

inline SyntheticRunResult RunSyntheticExperiment(const SyntheticRunConfig& config) {
  data_agent_system::storage::MemoryKVStore store;
  data_agent_system::runtime::TaskRuntime runtime(store);

  SyntheticRunResult result;
  result.task_count = config.task_count;
  result.tasks.reserve(config.task_count);
  for (std::size_t i = 0; i < config.task_count; ++i) {
    auto record = RunOneTask(i, config, runtime, store);
    AppendTaskRecord(&result, record);
  }
  return result;
}

inline SyntheticRunResult RunSyntheticExperimentWithStore(
    const SyntheticRunConfig& config,
    data_agent_system::runtime::TaskRuntime& runtime,
    data_agent_system::storage::VersionedKVStore& store) {
  SyntheticRunResult result;
  result.task_count = config.task_count;
  result.tasks.reserve(config.task_count);
  for (std::size_t i = 0; i < config.task_count; ++i) {
    AppendTaskRecord(&result, RunOneTask(i, config, runtime, store));
  }
  return result;
}

inline void WriteSyntheticCsv(const SyntheticRunResult& result, std::ostream& out) {
  out << "task_id,committed,branch_count,planned_loser_count,winner_commit_count,"
         "real_abort_count,conflict_abort_count,validation_fail_count,retry_count,"
         "commit_latency_us\n";
  for (const auto& task : result.tasks) {
    out << task.task_id << ','
        << (task.committed ? 1 : 0) << ','
        << task.branch_count << ','
        << task.planned_loser_count << ','
        << task.winner_commit_count << ','
        << task.real_abort_count << ','
        << task.conflict_abort_count << ','
        << task.validation_fail_count << ','
        << task.retry_count << ','
        << task.commit_latency_us << '\n';
  }
}

}  // namespace data_agent_system::workloads::synthetic
