#pragma once

#include <functional>
#include <string>
#include <unordered_map>

#include "data_agent_system/runtime/task_recovery_execution.h"

namespace data_agent_system::runtime {

class TaskRuntime;

using TaskContinuationHandler = std::function<bool(TaskRuntime&, TaskRecoveryExecution&)>;

class TaskContinuationRegistry {
 public:
  bool RegisterHandler(const std::string& workload_name,
                       const TaskContinuationHandler& handler) {
    if (workload_name.empty() || !handler) {
      return false;
    }
    handlers_[workload_name] = handler;
    return true;
  }

  const TaskContinuationHandler* FindHandler(const std::string& workload_name) const {
    const auto it = handlers_.find(workload_name);
    if (it == handlers_.end()) {
      return nullptr;
    }
    return &it->second;
  }

  bool Continue(TaskRuntime& runtime, TaskRecoveryExecution* execution) const {
    if (execution == nullptr) {
      return false;
    }

    std::string workload_name = execution->session.task.workload_name;
    if (workload_name.empty()) {
      workload_name = execution->recovery_plan.task.workload_name;
    }

    const auto* handler = FindHandler(workload_name);
    if (handler == nullptr) {
      return false;
    }
    return (*handler)(runtime, *execution);
  }

 private:
  std::unordered_map<std::string, TaskContinuationHandler> handlers_;
};

}  // namespace data_agent_system::runtime
