#pragma once

#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/workloads/agent_task/minimal_candidate_recovery.h"
#include "data_agent_system/workloads/kv_style/scripted_kv_task.h"
#include "data_agent_system/workloads/synthetic/synthetic_recovery.h"

namespace data_agent_system::workloads {

inline bool RegisterBuiltinRecoveryHandlers(
    data_agent_system::runtime::TaskRuntime& runtime) {
  bool registered = true;
  registered =
      data_agent_system::workloads::synthetic::RegisterSyntheticContinuation(runtime) &&
      registered;
  registered =
      data_agent_system::workloads::agent_task::RegisterMinimalCandidateContinuation(runtime) &&
      registered;
  registered =
      data_agent_system::workloads::kv_style::RegisterScriptedKvContinuationFromTaskMetadata(
          runtime) &&
      registered;
  return registered;
}

}  // namespace data_agent_system::workloads
