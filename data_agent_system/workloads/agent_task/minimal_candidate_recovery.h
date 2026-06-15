#pragma once

#include <string>

#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/runtime/task_continuation.h"
#include "data_agent_system/runtime/task_recovery_execution.h"
#include "data_agent_system/runtime/task_runtime.h"

namespace data_agent_system::workloads::agent_task {

inline bool ContinueRecoveredMinimalCandidateTask(
    data_agent_system::runtime::TaskRuntime& runtime,
    data_agent_system::runtime::TaskRecoveryExecution* execution,
    const std::string& result_value = "recovered_agent_result",
    double score = 101.0,
    const std::string& summary = "recovered minimal candidate task",
    const std::string& preferred_branch_id = "branch_B") {
  if (execution == nullptr) {
    return false;
  }

  std::string input_key;
  if (!execution->session.task.input_objects.empty()) {
    input_key = execution->session.task.input_objects.front().object_id;
  }

  std::string output_key;
  if (!execution->session.task.output_objects.empty()) {
    output_key = execution->session.task.output_objects.front().object_id;
  }
  if (output_key.empty()) {
    return false;
  }

  data_agent_system::runtime::TaskContinuationSpec spec;
  spec.preferred_branch_id = preferred_branch_id;
  spec.score = score;
  spec.summary = summary;
  return data_agent_system::runtime::ContinueRecoveredTask(
      runtime, execution, spec,
      [&](data_agent_system::runtime::TaskRuntime& callback_runtime,
          data_agent_system::runtime::TaskRecoveryExecution& callback_execution,
          const std::string& branch_id) {
        if (!input_key.empty()) {
          callback_runtime.Read(callback_execution.session, branch_id, input_key);
        }

        data_agent_system::intent::WriteIntent intent;
        intent.object_id = output_key;
        intent.intent_type = data_agent_system::intent::IntentType::kOverwrite;
        intent.payload = result_value;
        callback_runtime.Write(callback_execution.session, branch_id, output_key,
                               data_agent_system::cache::ObjectType::kText, intent);
        return true;
      });
}

inline bool RegisterMinimalCandidateContinuation(
    data_agent_system::runtime::TaskRuntime& runtime,
    const std::string& result_value = "recovered_agent_result",
    double score = 101.0,
    const std::string& summary = "recovered minimal candidate task",
    const std::string& preferred_branch_id = "branch_B") {
  return runtime.RegisterContinuationHandler(
      "agent_task.minimal_candidate_task",
      [=](data_agent_system::runtime::TaskRuntime& callback_runtime,
          data_agent_system::runtime::TaskRecoveryExecution& execution) {
        return ContinueRecoveredMinimalCandidateTask(callback_runtime, &execution, result_value,
                                                     score, summary, preferred_branch_id);
      });
}

}  // namespace data_agent_system::workloads::agent_task
