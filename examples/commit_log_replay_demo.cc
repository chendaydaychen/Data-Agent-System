#include <iostream>
#include <string>
#include <vector>

#include "data_agent_system/agent_txn/commit_log_replay.h"

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: commit_log_replay_demo <commit_log> [<commit_log> ...]\n";
    return 1;
  }

  std::vector<std::string> paths;
  for (int i = 1; i < argc; ++i) {
    paths.push_back(argv[i]);
  }

  const auto state = data_agent_system::agent_txn::ReplayCommitLogArtifacts(paths);
  for (const auto& pair : state) {
    std::cout << pair.first << '\t'
              << pair.second.expected_version << '\t'
              << pair.second.value << '\t'
              << pair.second.source_log_path << '\n';
  }
  return 0;
}
