#pragma once

#include <cstddef>
#include <fstream>
#include <string>

#include "data_agent_system/runtime/task_session.h"

namespace data_agent_system::runtime {

inline const char* kTaskEventLogHeader = "DAS_TASK_EVENT_LOG_V1";

inline std::string EscapeTaskEventLogText(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  for (const char ch : input) {
    if (ch == '\\' || ch == '\t' || ch == '\n') {
      output.push_back('\\');
      switch (ch) {
        case '\\':
          output.push_back('\\');
          break;
        case '\t':
          output.push_back('t');
          break;
        case '\n':
          output.push_back('n');
          break;
      }
    } else {
      output.push_back(ch);
    }
  }
  return output;
}

inline const char* TaskEventTypeName(TaskEventType type) {
  switch (type) {
    case TaskEventType::kSubmitTask:
      return "SUBMIT_TASK";
    case TaskEventType::kCreateBranch:
      return "CREATE_BRANCH";
    case TaskEventType::kRead:
      return "READ";
    case TaskEventType::kWrite:
      return "WRITE";
    case TaskEventType::kSavepoint:
      return "SAVEPOINT";
    case TaskEventType::kRollbackToSavepoint:
      return "ROLLBACK_TO_SAVEPOINT";
    case TaskEventType::kStageBranch:
      return "STAGE_BRANCH";
    case TaskEventType::kCommitAttempt:
      return "COMMIT_ATTEMPT";
    case TaskEventType::kCommitTask:
      return "COMMIT_TASK";
    case TaskEventType::kAbortTask:
      return "ABORT_TASK";
  }
  return "UNKNOWN";
}

inline void WriteTaskContextMetadataLines(const TaskContext& task, std::ofstream& output) {
  output << "input_object_count=" << task.input_objects.size() << '\n';
  for (std::size_t i = 0; i < task.input_objects.size(); ++i) {
    output << "input_object_" << i << "_id="
           << EscapeTaskEventLogText(task.input_objects[i].object_id) << '\n';
    output << "input_object_" << i << "_role="
           << EscapeTaskEventLogText(task.input_objects[i].object_role) << '\n';
  }

  output << "output_object_count=" << task.output_objects.size() << '\n';
  for (std::size_t i = 0; i < task.output_objects.size(); ++i) {
    output << "output_object_" << i << "_id="
           << EscapeTaskEventLogText(task.output_objects[i].object_id) << '\n';
    output << "output_object_" << i << "_role="
           << EscapeTaskEventLogText(task.output_objects[i].object_role) << '\n';
  }

  output << "task_metadata_count=" << task.metadata.size() << '\n';
  std::size_t index = 0;
  for (const auto& entry : task.metadata) {
    output << "task_metadata_" << index << "_key="
           << EscapeTaskEventLogText(entry.first) << '\n';
    output << "task_metadata_" << index << "_value="
           << EscapeTaskEventLogText(entry.second) << '\n';
    ++index;
  }
}

inline bool WriteTaskEventLogArtifact(const TaskSession& session, const std::string& output_path) {
  std::ofstream output(output_path.c_str(), std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  output << kTaskEventLogHeader << '\n';
  output << "task_id=" << EscapeTaskEventLogText(session.task.task_id) << '\n';
  output << "txn_id=" << EscapeTaskEventLogText(session.txn.txn_id) << '\n';
  output << "workload_name=" << EscapeTaskEventLogText(session.task.workload_name) << '\n';
  output << "planner_id=" << EscapeTaskEventLogText(session.task.planner_id) << '\n';
  WriteTaskContextMetadataLines(session.task, output);
  output << "task_phase=" << static_cast<int>(session.task.phase) << '\n';
  output << "commit_attempts=" << session.commit_attempts << '\n';
  output << "committed=" << (session.committed ? 1 : 0) << '\n';
  output << "event_count=" << session.events.size() << '\n';
  output << "[events]\n";
  for (const auto& event : session.events) {
    output << event.sequence_number << '\t'
           << TaskEventTypeName(event.type) << '\t'
           << EscapeTaskEventLogText(event.branch_id) << '\t'
           << EscapeTaskEventLogText(event.object_id) << '\t'
           << event.numeric_value << '\t'
           << EscapeTaskEventLogText(event.detail) << '\n';
  }
  output.flush();
  return output.good();
}

}  // namespace data_agent_system::runtime
