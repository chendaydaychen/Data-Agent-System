#pragma once

#include <cctype>
#include <string>

#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/runtime/task_continuation.h"
#include "data_agent_system/runtime/task_recovery_execution.h"
#include "data_agent_system/runtime/task_runtime.h"

namespace data_agent_system::workloads::synthetic {

inline std::string SyntheticRecoveryInputKey(
    const data_agent_system::runtime::TaskRecoveryExecution& execution) {
  if (!execution.session.task.input_objects.empty()) {
    return execution.session.task.input_objects.front().object_id;
  }
  return "input:" + execution.recovery_plan.decision.task_id;
}

inline std::string SyntheticRecoveryOutputKey(
    const data_agent_system::runtime::TaskRecoveryExecution& execution) {
  if (!execution.session.task.output_objects.empty()) {
    return execution.session.task.output_objects.front().object_id;
  }
  return "output:" + execution.recovery_plan.decision.task_id;
}

inline std::string SyntheticRecoveryResultValue(
    const data_agent_system::runtime::TaskRecoveryExecution& execution) {
  const std::string& task_id = execution.recovery_plan.decision.task_id;
  if (task_id == "task_resume") {
    return "recovered_resume_result";
  }

  const std::string prefix = "task_";
  if (task_id.rfind(prefix, 0) == 0 && task_id.size() > prefix.size()) {
    bool all_digits = true;
    for (std::size_t i = prefix.size(); i < task_id.size(); ++i) {
      if (!std::isdigit(static_cast<unsigned char>(task_id[i]))) {
        all_digits = false;
        break;
      }
    }
    if (all_digits) {
      return "recovered_result_" + task_id.substr(prefix.size());
    }
  }

  return "recovered_result";
}

inline double SyntheticRecoveryScore(
    const data_agent_system::runtime::TaskRecoveryExecution& execution) {
  if (execution.command_type ==
      data_agent_system::runtime::TaskRecoveryCommandType::kResumeFromSavepoint) {
    return 130.0;
  }
  return 120.0;
}

inline std::string SyntheticRecoverySummary(
    const data_agent_system::runtime::TaskRecoveryExecution& execution) {
  if (execution.command_type ==
      data_agent_system::runtime::TaskRecoveryCommandType::kResumeFromSavepoint) {
    return "recovered resume task";
  }
  return "recovered synthetic task";
}

inline bool ContinueRecoveredSyntheticTask(
    data_agent_system::runtime::TaskRuntime& runtime,
    data_agent_system::runtime::TaskRecoveryExecution* execution) {
  if (execution == nullptr) {
    return false;
  }

  const std::string input_key = SyntheticRecoveryInputKey(*execution);
  const std::string output_key = SyntheticRecoveryOutputKey(*execution);
  const std::string result_value = SyntheticRecoveryResultValue(*execution);

  data_agent_system::runtime::TaskContinuationSpec spec;
  spec.score = SyntheticRecoveryScore(*execution);
  spec.summary = SyntheticRecoverySummary(*execution);
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

inline bool RegisterSyntheticContinuation(
    data_agent_system::runtime::TaskRuntime& runtime) {
  return runtime.RegisterContinuationHandler(
      "synthetic.candidate_selection",
      [](data_agent_system::runtime::TaskRuntime& callback_runtime,
         data_agent_system::runtime::TaskRecoveryExecution& execution) {
        return ContinueRecoveredSyntheticTask(callback_runtime, &execution);
      });
}

}  // namespace data_agent_system::workloads::synthetic
