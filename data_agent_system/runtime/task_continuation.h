#pragma once

#include <functional>
#include <string>

#include "data_agent_system/runtime/task_recovery_execution.h"
#include "data_agent_system/runtime/task_runtime.h"

namespace data_agent_system::runtime {

struct TaskContinuationSpec {
  std::string preferred_branch_id;
  double score = 0.0;
  std::string summary;
};

using TaskContinuationCallback =
    std::function<bool(TaskRuntime&, TaskRecoveryExecution&, const std::string&)>;

inline bool ContinueRecoveredTask(TaskRuntime& runtime,
                                  TaskRecoveryExecution* execution,
                                  const TaskContinuationSpec& spec,
                                  const TaskContinuationCallback& callback) {
  if (execution == nullptr || !callback) {
    return false;
  }

  if (!runtime.ExecuteRecoveryExecution(execution)) {
    return false;
  }

  std::string branch_id = spec.preferred_branch_id;
  if (branch_id.empty()) {
    branch_id = execution->target_branch_id;
  }
  if (branch_id.empty() && !execution->session.plan.branch_plans.empty()) {
    branch_id = execution->session.plan.branch_plans.front().branch_id;
  }
  if (branch_id.empty()) {
    return false;
  }

  if (!callback(runtime, *execution, branch_id)) {
    return false;
  }

  runtime.StageBranch(execution->session, branch_id, spec.score, spec.summary);
  return runtime.CommitTask(execution->session);
}

}  // namespace data_agent_system::runtime
