#include <iostream>
#include <string>
#include <vector>

#include "data_agent_system/runtime/task_event_log_replay.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: task_event_log_replay_demo <task_log> [<task_log> ...]\n";
    return 1;
  }

  std::vector<std::string> paths;
  for (int i = 1; i < argc; ++i) {
    paths.push_back(argv[i]);
  }

  const auto states = data_agent_system::runtime::ReplayTaskEventLogArtifacts(paths);
  for (const auto& state : states) {
    std::cout << state.task_id << '\t'
              << state.txn_id << '\t'
              << state.task_phase << '\t'
              << state.commit_attempts << '\t'
              << state.committed << '\t'
              << state.event_count << '\t'
              << state.final_event_type << '\t'
              << state.final_branch_id << '\t'
              << state.read_event_count << '\t'
              << state.write_event_count << '\t'
              << state.source_log_path << '\n';
  }
  return 0;
}
