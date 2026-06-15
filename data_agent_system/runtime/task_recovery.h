#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data_agent_system/runtime/execution_plan.h"
#include "data_agent_system/runtime/task_event_log_replay.h"
#include "data_agent_system/runtime/task_session.h"

namespace data_agent_system::runtime {

enum class TaskRecoveryAction {
  kSkipCommitted,
  kRetryFromScratch,
  kResumeBranch,
  kResumeFromSavepoint,
  kRevalidateCommit,
};

struct TaskRecoveryDecision {
  std::string task_id;
  std::string txn_id;
  TaskRecoveryAction action = TaskRecoveryAction::kRetryFromScratch;
  std::string resume_branch_id;
  std::string resume_savepoint_id;
  std::string reason;
  std::string source_log_path;
};

struct TaskRecoveryPlan {
  TaskRecoveryDecision decision;
  TaskContext task;
  ExecutionPlan plan;
};

struct RecoveredTaskSession {
  TaskRecoveryPlan recovery_plan;
  TaskSession session;
};

inline const char* TaskRecoveryActionName(TaskRecoveryAction action) {
  switch (action) {
    case TaskRecoveryAction::kSkipCommitted:
      return "SKIP_COMMITTED";
    case TaskRecoveryAction::kRetryFromScratch:
      return "RETRY_FROM_SCRATCH";
    case TaskRecoveryAction::kResumeBranch:
      return "RESUME_BRANCH";
    case TaskRecoveryAction::kResumeFromSavepoint:
      return "RESUME_FROM_SAVEPOINT";
    case TaskRecoveryAction::kRevalidateCommit:
      return "REVALIDATE_COMMIT";
  }
  return "UNKNOWN";
}

inline TaskPhase TaskPhaseFromPersistedValue(const std::string& persisted_value) {
  if (persisted_value == "1") {
    return TaskPhase::kRunning;
  }
  if (persisted_value == "2") {
    return TaskPhase::kCommitted;
  }
  if (persisted_value == "3") {
    return TaskPhase::kAborted;
  }
  return TaskPhase::kCreated;
}

inline TaskEventType TaskEventTypeFromName(const std::string& name) {
  if (name == "SUBMIT_TASK") {
    return TaskEventType::kSubmitTask;
  }
  if (name == "CREATE_BRANCH") {
    return TaskEventType::kCreateBranch;
  }
  if (name == "READ") {
    return TaskEventType::kRead;
  }
  if (name == "WRITE") {
    return TaskEventType::kWrite;
  }
  if (name == "SAVEPOINT") {
    return TaskEventType::kSavepoint;
  }
  if (name == "ROLLBACK_TO_SAVEPOINT") {
    return TaskEventType::kRollbackToSavepoint;
  }
  if (name == "STAGE_BRANCH") {
    return TaskEventType::kStageBranch;
  }
  if (name == "COMMIT_ATTEMPT") {
    return TaskEventType::kCommitAttempt;
  }
  if (name == "COMMIT_TASK") {
    return TaskEventType::kCommitTask;
  }
  if (name == "ABORT_TASK") {
    return TaskEventType::kAbortTask;
  }
  return TaskEventType::kSubmitTask;
}

inline std::size_t ParseTaskMetadataSize(const std::string& value) {
  return static_cast<std::size_t>(std::strtoull(value.c_str(), nullptr, 10));
}

inline void RestoreTaskContextFromArtifactMetadata(const ParsedTaskEventLogArtifact& artifact,
                                                   TaskContext* task) {
  if (task == nullptr) {
    return;
  }

  auto metadata_value = [&](const std::string& key) -> std::string {
    const auto it = artifact.metadata.find(key);
    return it == artifact.metadata.end() ? std::string() : it->second;
  };

  task->input_objects.clear();
  const std::size_t input_count = ParseTaskMetadataSize(metadata_value("input_object_count"));
  for (std::size_t i = 0; i < input_count; ++i) {
    const std::string object_id = metadata_value("input_object_" + std::to_string(i) + "_id");
    if (object_id.empty()) {
      continue;
    }
    task->AddInputObject(object_id,
                         metadata_value("input_object_" + std::to_string(i) + "_role"));
  }

  task->output_objects.clear();
  const std::size_t output_count = ParseTaskMetadataSize(metadata_value("output_object_count"));
  for (std::size_t i = 0; i < output_count; ++i) {
    const std::string object_id = metadata_value("output_object_" + std::to_string(i) + "_id");
    if (object_id.empty()) {
      continue;
    }
    task->AddOutputObject(object_id,
                          metadata_value("output_object_" + std::to_string(i) + "_role"));
  }

  task->metadata.clear();
  const std::size_t metadata_count = ParseTaskMetadataSize(metadata_value("task_metadata_count"));
  for (std::size_t i = 0; i < metadata_count; ++i) {
    const std::string key = metadata_value("task_metadata_" + std::to_string(i) + "_key");
    if (key.empty()) {
      continue;
    }
    task->SetMetadata(key,
                      metadata_value("task_metadata_" + std::to_string(i) + "_value"));
  }
}

inline TaskRecoveryDecision RecoverTaskSessionDecision(
    const std::string& path,
    const ParsedTaskEventLogArtifact& artifact) {
  TaskRecoveryDecision decision;
  auto metadata_value = [&](const std::string& key) -> std::string {
    const auto it = artifact.metadata.find(key);
    return it == artifact.metadata.end() ? std::string() : it->second;
  };

  decision.task_id = metadata_value("task_id");
  decision.txn_id = metadata_value("txn_id");
  decision.source_log_path = path;

  const ParsedTaskEvent* final_event = artifact.events.empty() ? nullptr : &artifact.events.back();
  const std::string task_phase = metadata_value("task_phase");
  const bool committed = metadata_value("committed") == "1";

  auto find_latest_savepoint = [&](const std::string& branch_id) -> std::string {
    for (auto it = artifact.events.rbegin(); it != artifact.events.rend(); ++it) {
      if (it->branch_id == branch_id && it->event_type == "SAVEPOINT") {
        return it->detail;
      }
    }
    return {};
  };

  auto find_latest_branch = [&]() -> std::string {
    for (auto it = artifact.events.rbegin(); it != artifact.events.rend(); ++it) {
      if (!it->branch_id.empty()) {
        return it->branch_id;
      }
    }
    return {};
  };

  if (committed || task_phase == "2" ||
      (final_event != nullptr && final_event->event_type == "COMMIT_TASK")) {
    decision.action = TaskRecoveryAction::kSkipCommitted;
    decision.resume_branch_id = final_event == nullptr ? std::string() : final_event->branch_id;
    decision.reason = "task already committed";
    return decision;
  }

  if (final_event != nullptr && final_event->event_type == "ABORT_TASK") {
    decision.action = TaskRecoveryAction::kRetryFromScratch;
    decision.resume_branch_id = final_event->branch_id;
    decision.reason = final_event->detail.empty() ? "task aborted" : final_event->detail;
    return decision;
  }

  if (task_phase == "3") {
    decision.action = TaskRecoveryAction::kRetryFromScratch;
    decision.reason = "task phase is aborted";
    return decision;
  }

  if (final_event != nullptr && final_event->event_type == "COMMIT_ATTEMPT") {
    decision.action = TaskRecoveryAction::kRevalidateCommit;
    decision.reason = "commit attempt recorded without terminal outcome";
    return decision;
  }

  decision.resume_branch_id = find_latest_branch();
  decision.resume_savepoint_id = find_latest_savepoint(decision.resume_branch_id);
  if (!decision.resume_savepoint_id.empty()) {
    decision.action = TaskRecoveryAction::kResumeFromSavepoint;
    decision.reason = "resume from latest savepoint " + decision.resume_savepoint_id;
    return decision;
  }

  decision.action = TaskRecoveryAction::kResumeBranch;
  decision.reason = decision.resume_branch_id.empty() ? "resume task execution"
                                                      : "resume branch execution";
  return decision;
}

inline std::vector<TaskRecoveryDecision> RecoverTaskSessionDecisions(
    const std::vector<std::string>& task_log_paths) {
  std::vector<TaskRecoveryDecision> decisions;
  for (const auto& path : task_log_paths) {
    ParsedTaskEventLogArtifact artifact;
    if (!ParseTaskEventLogArtifact(path, &artifact)) {
      continue;
    }
    decisions.push_back(RecoverTaskSessionDecision(path, artifact));
  }
  return decisions;
}

inline TaskRecoveryPlan BuildTaskRecoveryPlan(
    const std::string& path,
    const ParsedTaskEventLogArtifact& artifact) {
  TaskRecoveryPlan recovery_plan;
  recovery_plan.decision = RecoverTaskSessionDecision(path, artifact);

  auto metadata_value = [&](const std::string& key) -> std::string {
    const auto it = artifact.metadata.find(key);
    return it == artifact.metadata.end() ? std::string() : it->second;
  };

  recovery_plan.task.task_id = metadata_value("task_id");
  recovery_plan.task.objective = "recovered task session";
  recovery_plan.task.workload_name = metadata_value("workload_name");
  if (recovery_plan.task.workload_name.empty()) {
    recovery_plan.task.workload_name = "runtime.recovered_from_task_log";
  }
  recovery_plan.task.planner_id = metadata_value("planner_id");
  if (recovery_plan.task.planner_id.empty()) {
    recovery_plan.task.planner_id = "task_recovery_manager";
  }
  RestoreTaskContextFromArtifactMetadata(artifact, &recovery_plan.task);
  recovery_plan.task.phase = TaskPhaseFromPersistedValue(metadata_value("task_phase"));
  recovery_plan.task.SetMetadata("source_log", path);
  recovery_plan.task.SetMetadata("recovery_action",
                                 TaskRecoveryActionName(recovery_plan.decision.action));
  recovery_plan.task.SetMetadata("recovery_reason", recovery_plan.decision.reason);
  recovery_plan.task.SetMetadata("resume_branch_id", recovery_plan.decision.resume_branch_id);
  recovery_plan.task.SetMetadata("resume_savepoint_id",
                                 recovery_plan.decision.resume_savepoint_id);

  for (const auto& event : artifact.events) {
    if (event.event_type == "SUBMIT_TASK" && !event.detail.empty()) {
      recovery_plan.task.objective = event.detail;
    }
    if (event.event_type == "READ" && recovery_plan.task.input_objects.empty()) {
      recovery_plan.task.AddInputObject(event.object_id, "recovered_input");
    }
    if (event.event_type == "WRITE" && recovery_plan.task.output_objects.empty()) {
      recovery_plan.task.AddOutputObject(event.object_id, "recovered_output");
    }
    if (event.event_type == "CREATE_BRANCH") {
      recovery_plan.plan.AddBranchPlan(event.branch_id, event.detail, "recovered branch",
                                       static_cast<double>(event.numeric_value));
    }
  }

  if (recovery_plan.plan.branch_plans.empty() &&
      !recovery_plan.decision.resume_branch_id.empty()) {
    recovery_plan.plan.AddBranchPlan(recovery_plan.decision.resume_branch_id,
                                     recovery_plan.decision.resume_branch_id + "_candidate",
                                     "recovered fallback branch", 0.0);
  }

  return recovery_plan;
}

inline RecoveredTaskSession RecoverTaskSessionFromArtifact(
    const std::string& path,
    const ParsedTaskEventLogArtifact& artifact) {
  RecoveredTaskSession recovered;
  recovered.recovery_plan = BuildTaskRecoveryPlan(path, artifact);
  recovered.session.task = recovered.recovery_plan.task;
  recovered.session.plan = recovered.recovery_plan.plan;
  recovered.session.txn.txn_id = recovered.recovery_plan.decision.txn_id;
  recovered.session.txn.task_id = recovered.recovery_plan.decision.task_id;
  recovered.session.commit_attempts =
      static_cast<std::size_t>(std::strtoull(artifact.metadata.at("commit_attempts").c_str(),
                                            nullptr, 10));
  recovered.session.committed = artifact.metadata.at("committed") == "1";

  for (const auto& branch_plan : recovered.session.plan.branch_plans) {
    data_agent_system::branch::BranchContext branch;
    branch.branch_id = branch_plan.branch_id;
    if (!recovered.recovery_plan.decision.resume_branch_id.empty() &&
        branch.branch_id == recovered.recovery_plan.decision.resume_branch_id) {
      branch.status = data_agent_system::branch::BranchStatus::kRunning;
    }
    recovered.session.txn.branches.push_back(branch);
  }

  if (recovered.session.committed) {
    recovered.session.txn.status = data_agent_system::agent_txn::TxnStatus::kCommitted;
    recovered.session.txn.winner_branch_id = recovered.recovery_plan.decision.resume_branch_id;
  } else if (recovered.recovery_plan.decision.action == TaskRecoveryAction::kRetryFromScratch) {
    recovered.session.txn.status = data_agent_system::agent_txn::TxnStatus::kAborted;
    recovered.session.txn.winner_branch_id = recovered.recovery_plan.decision.resume_branch_id;
  } else {
    recovered.session.txn.status = data_agent_system::agent_txn::TxnStatus::kRunning;
  }

  for (const auto& parsed_event : artifact.events) {
    TaskEvent event;
    event.sequence_number = parsed_event.sequence_number;
    event.type = TaskEventTypeFromName(parsed_event.event_type);
    event.branch_id = parsed_event.branch_id;
    event.object_id = parsed_event.object_id;
    event.detail = parsed_event.detail;
    event.numeric_value = parsed_event.numeric_value;
    recovered.session.events.push_back(event);
  }

  return recovered;
}

inline std::vector<TaskRecoveryPlan> BuildTaskRecoveryPlans(
    const std::vector<std::string>& task_log_paths) {
  std::vector<TaskRecoveryPlan> plans;
  for (const auto& path : task_log_paths) {
    ParsedTaskEventLogArtifact artifact;
    if (!ParseTaskEventLogArtifact(path, &artifact)) {
      continue;
    }
    plans.push_back(BuildTaskRecoveryPlan(path, artifact));
  }
  return plans;
}

inline std::vector<RecoveredTaskSession> RecoverTaskSessions(
    const std::vector<std::string>& task_log_paths) {
  std::vector<RecoveredTaskSession> sessions;
  for (const auto& path : task_log_paths) {
    ParsedTaskEventLogArtifact artifact;
    if (!ParseTaskEventLogArtifact(path, &artifact)) {
      continue;
    }
    sessions.push_back(RecoverTaskSessionFromArtifact(path, artifact));
  }
  return sessions;
}

}  // namespace data_agent_system::runtime
