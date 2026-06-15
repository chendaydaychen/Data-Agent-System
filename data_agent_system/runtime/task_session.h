#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "data_agent_system/agent_txn/agent_txn_context.h"
#include "data_agent_system/runtime/execution_plan.h"
#include "data_agent_system/runtime/task_context.h"

namespace data_agent_system::runtime {

enum class TaskEventType {
  kSubmitTask,
  kCreateBranch,
  kRead,
  kWrite,
  kSavepoint,
  kRollbackToSavepoint,
  kStageBranch,
  kCommitAttempt,
  kCommitTask,
  kAbortTask,
};

struct TaskEvent {
  std::size_t sequence_number = 0;
  TaskEventType type = TaskEventType::kSubmitTask;
  std::string branch_id;
  std::string object_id;
  std::string detail;
  std::int64_t numeric_value = 0;
};

struct TaskSession {
  TaskContext task;
  ExecutionPlan plan;
  data_agent_system::agent_txn::AgentTxnContext txn;
  std::size_t commit_attempts = 0;
  bool committed = false;
  std::vector<TaskEvent> events;

  void MarkRunning() { task.phase = TaskPhase::kRunning; }

  void MarkCommitted() {
    task.phase = TaskPhase::kCommitted;
    committed = true;
  }

  void MarkAborted() {
    task.phase = TaskPhase::kAborted;
    committed = false;
  }

  void RecordEvent(TaskEventType type,
                   const std::string& branch_id = std::string(),
                   const std::string& object_id = std::string(),
                   const std::string& detail = std::string(),
                   std::int64_t numeric_value = 0) {
    TaskEvent event;
    event.sequence_number = events.size();
    event.type = type;
    event.branch_id = branch_id;
    event.object_id = object_id;
    event.detail = detail;
    event.numeric_value = numeric_value;
    events.push_back(event);
  }
};

}  // namespace data_agent_system::runtime
