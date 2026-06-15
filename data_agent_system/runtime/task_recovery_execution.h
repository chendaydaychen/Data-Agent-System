#pragma once

#include <string>
#include <vector>

#include "data_agent_system/runtime/task_recovery.h"

namespace data_agent_system::runtime {

enum class TaskRecoveryCommandType {
  kNoop,
  kSubmitFreshTask,
  kResumeBranch,
  kResumeFromSavepoint,
  kRevalidateCommit,
};

struct TaskRecoveryExecution {
  TaskRecoveryPlan recovery_plan;
  TaskSession session;
  TaskRecoveryCommandType command_type = TaskRecoveryCommandType::kNoop;
  std::string target_branch_id;
  std::string target_savepoint_id;
  std::string summary;
};

inline const char* TaskRecoveryCommandTypeName(TaskRecoveryCommandType type) {
  switch (type) {
    case TaskRecoveryCommandType::kNoop:
      return "NOOP";
    case TaskRecoveryCommandType::kSubmitFreshTask:
      return "SUBMIT_FRESH_TASK";
    case TaskRecoveryCommandType::kResumeBranch:
      return "RESUME_BRANCH";
    case TaskRecoveryCommandType::kResumeFromSavepoint:
      return "RESUME_FROM_SAVEPOINT";
    case TaskRecoveryCommandType::kRevalidateCommit:
      return "REVALIDATE_COMMIT";
  }
  return "UNKNOWN";
}

inline TaskRecoveryExecution BuildTaskRecoveryExecution(
    const RecoveredTaskSession& recovered) {
  TaskRecoveryExecution execution;
  execution.recovery_plan = recovered.recovery_plan;
  execution.session = recovered.session;
  execution.target_branch_id = recovered.recovery_plan.decision.resume_branch_id;
  execution.target_savepoint_id = recovered.recovery_plan.decision.resume_savepoint_id;

  switch (recovered.recovery_plan.decision.action) {
    case TaskRecoveryAction::kSkipCommitted:
      execution.command_type = TaskRecoveryCommandType::kNoop;
      execution.summary = "task already committed; no recovery work required";
      break;
    case TaskRecoveryAction::kRetryFromScratch:
      execution.command_type = TaskRecoveryCommandType::kSubmitFreshTask;
      execution.summary = "restart task from a fresh submission";
      break;
    case TaskRecoveryAction::kResumeBranch:
      execution.command_type = TaskRecoveryCommandType::kResumeBranch;
      execution.summary = "resume execution from the latest branch state";
      break;
    case TaskRecoveryAction::kResumeFromSavepoint:
      execution.command_type = TaskRecoveryCommandType::kResumeFromSavepoint;
      execution.summary = "resume execution from the latest recorded savepoint";
      break;
    case TaskRecoveryAction::kRevalidateCommit:
      execution.command_type = TaskRecoveryCommandType::kRevalidateCommit;
      execution.summary = "re-run validation and conditional commit";
      break;
  }

  execution.session.task.SetMetadata("recovery_command",
                                     TaskRecoveryCommandTypeName(execution.command_type));
  execution.session.task.SetMetadata("recovery_summary", execution.summary);
  return execution;
}

inline std::vector<TaskRecoveryExecution> BuildTaskRecoveryExecutions(
    const std::vector<RecoveredTaskSession>& recovered_sessions) {
  std::vector<TaskRecoveryExecution> executions;
  executions.reserve(recovered_sessions.size());
  for (const auto& recovered : recovered_sessions) {
    executions.push_back(BuildTaskRecoveryExecution(recovered));
  }
  return executions;
}

}  // namespace data_agent_system::runtime
