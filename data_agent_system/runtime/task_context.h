#pragma once

#include <stdexcept>
#include <unordered_map>
#include <string>
#include <vector>

namespace data_agent_system::runtime {

enum class TaskPhase {
  kCreated,
  kRunning,
  kCommitted,
  kAborted,
};

struct ObjectReference {
  std::string object_id;
  std::string object_role;
};

struct TaskContext {
  std::string task_id;
  std::string objective;
  std::string workload_name;
  std::string planner_id;
  TaskPhase phase = TaskPhase::kCreated;
  std::vector<ObjectReference> input_objects;
  std::vector<ObjectReference> output_objects;
  std::unordered_map<std::string, std::string> metadata;

  void AddInputObject(const std::string& object_id, const std::string& object_role) {
    input_objects.push_back(ObjectReference{object_id, object_role});
  }

  void AddOutputObject(const std::string& object_id, const std::string& object_role) {
    output_objects.push_back(ObjectReference{object_id, object_role});
  }

  void SetMetadata(const std::string& key, const std::string& value) { metadata[key] = value; }

  void Validate() const {
    if (task_id.empty()) {
      throw std::runtime_error("task context requires a non-empty task_id");
    }
    if (objective.empty()) {
      throw std::runtime_error("task context requires a non-empty objective");
    }
  }
};

}  // namespace data_agent_system::runtime
