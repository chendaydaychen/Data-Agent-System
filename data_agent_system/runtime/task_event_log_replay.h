#pragma once

#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

namespace data_agent_system::runtime {

struct ParsedTaskEvent {
  std::size_t sequence_number = 0;
  std::string event_type;
  std::string branch_id;
  std::string object_id;
  std::int64_t numeric_value = 0;
  std::string detail;
};

struct ParsedTaskEventLogArtifact {
  std::unordered_map<std::string, std::string> metadata;
  std::vector<ParsedTaskEvent> events;
};

struct ReplayedTaskSessionState {
  std::string task_id;
  std::string txn_id;
  std::string task_phase;
  std::size_t commit_attempts = 0;
  bool committed = false;
  std::size_t event_count = 0;
  std::string final_event_type;
  std::string final_branch_id;
  std::size_t write_event_count = 0;
  std::size_t read_event_count = 0;
  std::string source_log_path;
};

inline std::string UnescapeTaskEventLogText(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  bool escaped = false;
  for (const char ch : input) {
    if (escaped) {
      switch (ch) {
        case 't':
          output.push_back('\t');
          break;
        case 'n':
          output.push_back('\n');
          break;
        case '\\':
          output.push_back('\\');
          break;
        default:
          output.push_back(ch);
          break;
      }
      escaped = false;
    } else if (ch == '\\') {
      escaped = true;
    } else {
      output.push_back(ch);
    }
  }
  if (escaped) {
    output.push_back('\\');
  }
  return output;
}

inline bool ParseTaskEventLogArtifact(const std::string& path, ParsedTaskEventLogArtifact* artifact) {
  if (artifact == nullptr) {
    return false;
  }

  std::ifstream input(path.c_str());
  if (!input.is_open()) {
    return false;
  }

  artifact->metadata.clear();
  artifact->events.clear();

  std::string line;
  if (!std::getline(input, line)) {
    return false;
  }
  if (line != "DAS_TASK_EVENT_LOG_V1") {
    return false;
  }

  bool in_events = false;
  while (std::getline(input, line)) {
    if (line == "[events]") {
      in_events = true;
      continue;
    }
    if (line.empty()) {
      continue;
    }

    if (!in_events) {
      const std::size_t pos = line.find('=');
      if (pos == std::string::npos) {
        return false;
      }
      artifact->metadata[line.substr(0, pos)] = UnescapeTaskEventLogText(line.substr(pos + 1));
      continue;
    }

    std::vector<std::string> parts;
    std::size_t start = 0;
    while (true) {
      const std::size_t tab = line.find('\t', start);
      if (tab == std::string::npos) {
        parts.push_back(line.substr(start));
        break;
      }
      parts.push_back(line.substr(start, tab - start));
      start = tab + 1;
    }
    if (parts.size() != 6) {
      return false;
    }

    ParsedTaskEvent event;
    event.sequence_number = static_cast<std::size_t>(std::strtoull(parts[0].c_str(), nullptr, 10));
    event.event_type = parts[1];
    event.branch_id = UnescapeTaskEventLogText(parts[2]);
    event.object_id = UnescapeTaskEventLogText(parts[3]);
    event.numeric_value = static_cast<std::int64_t>(std::strtoll(parts[4].c_str(), nullptr, 10));
    event.detail = UnescapeTaskEventLogText(parts[5]);
    artifact->events.push_back(event);
  }

  return true;
}

inline std::vector<ReplayedTaskSessionState> ReplayTaskEventLogArtifacts(
    const std::vector<std::string>& task_log_paths) {
  std::vector<ReplayedTaskSessionState> states;
  for (const auto& path : task_log_paths) {
    ParsedTaskEventLogArtifact artifact;
    if (!ParseTaskEventLogArtifact(path, &artifact)) {
      continue;
    }

    ReplayedTaskSessionState state;
    state.task_id = artifact.metadata["task_id"];
    state.txn_id = artifact.metadata["txn_id"];
    state.task_phase = artifact.metadata["task_phase"];
    state.commit_attempts =
        static_cast<std::size_t>(std::strtoull(artifact.metadata["commit_attempts"].c_str(), nullptr, 10));
    state.committed = artifact.metadata["committed"] == "1";
    state.event_count = artifact.events.size();
    state.source_log_path = path;

    for (const auto& event : artifact.events) {
      if (event.event_type == "READ") {
        state.read_event_count += 1;
      } else if (event.event_type == "WRITE") {
        state.write_event_count += 1;
      }
    }
    if (!artifact.events.empty()) {
      state.final_event_type = artifact.events.back().event_type;
      state.final_branch_id = artifact.events.back().branch_id;
    }

    states.push_back(state);
  }
  return states;
}

}  // namespace data_agent_system::runtime
