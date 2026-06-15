#pragma once

#include <dirent.h>

#include <algorithm>
#include <string>
#include <vector>

#include "data_agent_system/runtime/task_recovery.h"

namespace data_agent_system::runtime {

class TaskRecoveryManager {
 public:
  std::vector<std::string> ListTaskLogs(const std::string& task_log_dir) const {
    std::vector<std::string> paths;
    DIR* dir = opendir(task_log_dir.c_str());
    if (dir == nullptr) {
      return paths;
    }

    while (true) {
      dirent* entry = readdir(dir);
      if (entry == nullptr) {
        break;
      }
      const std::string name = entry->d_name;
      if (name == "." || name == "..") {
        continue;
      }
      if (!EndsWith(name, ".task.log")) {
        continue;
      }
      paths.push_back(task_log_dir + "/" + name);
    }

    closedir(dir);
    std::sort(paths.begin(), paths.end());
    return paths;
  }

  std::vector<TaskRecoveryDecision> RecoverFromDirectory(const std::string& task_log_dir) const {
    return RecoverTaskSessionDecisions(ListTaskLogs(task_log_dir));
  }

  std::vector<TaskRecoveryPlan> BuildPlansFromDirectory(const std::string& task_log_dir) const {
    return BuildTaskRecoveryPlans(ListTaskLogs(task_log_dir));
  }

  std::vector<RecoveredTaskSession> RecoverSessionsFromDirectory(
      const std::string& task_log_dir) const {
    return RecoverTaskSessions(ListTaskLogs(task_log_dir));
  }

 private:
  static bool EndsWith(const std::string& value, const std::string& suffix) {
    if (value.size() < suffix.size()) {
      return false;
    }
    return value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0;
  }
};

}  // namespace data_agent_system::runtime
