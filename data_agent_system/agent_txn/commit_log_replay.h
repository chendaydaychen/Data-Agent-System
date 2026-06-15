#pragma once

#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#include "data_agent_system/agent_txn/commit_log_io.h"

namespace data_agent_system::agent_txn {

struct ParsedCommitLogEntry {
  std::string key;
  std::uint64_t expected_version = 0;
  std::string value;
};

struct ParsedCommitLogArtifact {
  std::unordered_map<std::string, std::string> metadata;
  std::vector<ParsedCommitLogEntry> entries;
};

struct ReplayedObjectState {
  std::string value;
  std::uint64_t expected_version = 0;
  std::string source_log_path;
};

inline bool ParseCommitLogArtifact(const std::string& path, ParsedCommitLogArtifact* artifact) {
  if (artifact == nullptr) {
    return false;
  }

  std::ifstream input(path.c_str());
  if (!input.is_open()) {
    return false;
  }

  artifact->metadata.clear();
  artifact->entries.clear();

  std::string line;
  if (!std::getline(input, line)) {
    return false;
  }
  if (line != "DAS_COMMIT_LOG_V1") {
    return false;
  }

  bool in_entries = false;
  while (std::getline(input, line)) {
    if (line == "[entries]") {
      in_entries = true;
      continue;
    }
    if (line.empty()) {
      continue;
    }

    if (!in_entries) {
      const std::size_t pos = line.find('=');
      if (pos == std::string::npos) {
        return false;
      }
      artifact->metadata[line.substr(0, pos)] = UnescapeCommitLogText(line.substr(pos + 1));
      continue;
    }

    std::istringstream stream(line);
    std::string key;
    std::string expected_version_text;
    std::string value;
    if (!std::getline(stream, key, '\t') || !std::getline(stream, expected_version_text, '\t') ||
        !std::getline(stream, value)) {
      return false;
    }

    ParsedCommitLogEntry entry;
    entry.key = UnescapeCommitLogText(key);
    entry.expected_version =
        static_cast<std::uint64_t>(std::strtoull(expected_version_text.c_str(), nullptr, 10));
    entry.value = UnescapeCommitLogText(value);
    artifact->entries.push_back(entry);
  }
  return true;
}

inline std::unordered_map<std::string, ReplayedObjectState> ReplayCommitLogArtifacts(
    const std::vector<std::string>& commit_log_paths) {
  std::unordered_map<std::string, ReplayedObjectState> state;
  for (const auto& path : commit_log_paths) {
    ParsedCommitLogArtifact artifact;
    if (!ParseCommitLogArtifact(path, &artifact)) {
      continue;
    }
    const auto status_it = artifact.metadata.find("status");
    if (status_it == artifact.metadata.end() || status_it->second != "COMMITTED") {
      continue;
    }
    for (const auto& entry : artifact.entries) {
      ReplayedObjectState replayed;
      replayed.value = entry.value;
      replayed.expected_version = entry.expected_version;
      replayed.source_log_path = path;
      state[entry.key] = replayed;
    }
  }
  return state;
}

}  // namespace data_agent_system::agent_txn
