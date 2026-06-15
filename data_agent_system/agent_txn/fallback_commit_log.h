#pragma once

#include <cstdlib>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "data_agent_system/agent_txn/commit_log_io.h"

namespace data_agent_system::agent_txn {

inline const char* kFallbackCommitHeader = "DAS_FALLBACK_COMMIT_V1";

enum class FallbackCommitPhase {
  kPrepared,
  kApplying,
  kRollingBack,
  kCommitted,
  kRolledBack,
};

struct FallbackCommitEntry {
  std::string key;
  std::uint64_t expected_version = 0;
  std::string target_value;
  std::string previous_value;
  std::uint64_t previous_version = 0;
  bool previous_exists = false;
};

struct FallbackCommitArtifact {
  std::string txn_id;
  std::string task_id;
  FallbackCommitPhase phase = FallbackCommitPhase::kPrepared;
  std::size_t applied_count = 0;
  std::vector<FallbackCommitEntry> entries;
};

inline const char* FallbackCommitPhaseName(FallbackCommitPhase phase) {
  switch (phase) {
    case FallbackCommitPhase::kPrepared:
      return "PREPARED";
    case FallbackCommitPhase::kApplying:
      return "APPLYING";
    case FallbackCommitPhase::kRollingBack:
      return "ROLLING_BACK";
    case FallbackCommitPhase::kCommitted:
      return "COMMITTED";
    case FallbackCommitPhase::kRolledBack:
      return "ROLLED_BACK";
  }
  return "PREPARED";
}

inline FallbackCommitPhase FallbackCommitPhaseFromName(const std::string& name) {
  if (name == "APPLYING") {
    return FallbackCommitPhase::kApplying;
  }
  if (name == "ROLLING_BACK") {
    return FallbackCommitPhase::kRollingBack;
  }
  if (name == "COMMITTED") {
    return FallbackCommitPhase::kCommitted;
  }
  if (name == "ROLLED_BACK") {
    return FallbackCommitPhase::kRolledBack;
  }
  return FallbackCommitPhase::kPrepared;
}

inline bool WriteFallbackCommitArtifact(const FallbackCommitArtifact& artifact,
                                        const std::string& output_path) {
  const std::string temp_path = output_path + ".tmp";
  std::ofstream output(temp_path.c_str(), std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  output << kFallbackCommitHeader << '\n';
  output << "txn_id=" << EscapeCommitLogText(artifact.txn_id) << '\n';
  output << "task_id=" << EscapeCommitLogText(artifact.task_id) << '\n';
  output << "phase=" << FallbackCommitPhaseName(artifact.phase) << '\n';
  output << "applied_count=" << artifact.applied_count << '\n';
  output << "entry_count=" << artifact.entries.size() << '\n';
  output << "[entries]\n";
  for (const auto& entry : artifact.entries) {
    output << EscapeCommitLogText(entry.key) << '\t'
           << entry.expected_version << '\t'
           << EscapeCommitLogText(entry.target_value) << '\t'
           << EscapeCommitLogText(entry.previous_value) << '\t'
           << entry.previous_version << '\t'
           << (entry.previous_exists ? 1 : 0) << '\n';
  }
  output.flush();
  if (!output.good()) {
    output.close();
    std::remove(temp_path.c_str());
    return false;
  }
  output.close();
  if (std::rename(temp_path.c_str(), output_path.c_str()) != 0) {
    std::remove(temp_path.c_str());
    return false;
  }
  return true;
}

inline bool ParseFallbackCommitArtifact(const std::string& input_path,
                                        FallbackCommitArtifact* artifact) {
  if (artifact == nullptr) {
    return false;
  }

  std::ifstream input(input_path.c_str());
  if (!input.is_open()) {
    return false;
  }

  std::string line;
  if (!std::getline(input, line) || line != kFallbackCommitHeader) {
    return false;
  }

  artifact->entries.clear();
  bool in_entries = false;
  while (std::getline(input, line)) {
    if (line.empty()) {
      continue;
    }
    if (line == "[entries]") {
      in_entries = true;
      continue;
    }

    if (!in_entries) {
      const auto delimiter = line.find('=');
      if (delimiter == std::string::npos) {
        return false;
      }
      const std::string key = line.substr(0, delimiter);
      const std::string value = line.substr(delimiter + 1);
      if (key == "txn_id") {
        artifact->txn_id = value;
      } else if (key == "task_id") {
        artifact->task_id = value;
      } else if (key == "phase") {
        artifact->phase = FallbackCommitPhaseFromName(value);
      } else if (key == "applied_count") {
        artifact->applied_count = static_cast<std::size_t>(
            std::strtoull(value.c_str(), nullptr, 10));
      }
      continue;
    }

    std::istringstream stream(line);
    std::string encoded_key;
    std::string expected_version_text;
    std::string encoded_target_value;
    std::string encoded_previous_value;
    std::string previous_version_text;
    std::string previous_exists_text;
    if (!std::getline(stream, encoded_key, '\t') ||
        !std::getline(stream, expected_version_text, '\t') ||
        !std::getline(stream, encoded_target_value, '\t') ||
        !std::getline(stream, encoded_previous_value, '\t') ||
        !std::getline(stream, previous_version_text, '\t') ||
        !std::getline(stream, previous_exists_text)) {
      return false;
    }

    FallbackCommitEntry entry;
    entry.key = encoded_key;
    entry.expected_version =
        static_cast<std::uint64_t>(std::strtoull(expected_version_text.c_str(), nullptr, 10));
    entry.target_value = encoded_target_value;
    entry.previous_value = encoded_previous_value;
    entry.previous_version =
        static_cast<std::uint64_t>(std::strtoull(previous_version_text.c_str(), nullptr, 10));
    entry.previous_exists = previous_exists_text == "1";
    artifact->entries.push_back(entry);
  }

  artifact->txn_id = UnescapeCommitLogText(artifact->txn_id);
  artifact->task_id = UnescapeCommitLogText(artifact->task_id);
  for (auto& entry : artifact->entries) {
    entry.key = UnescapeCommitLogText(entry.key);
    entry.target_value = UnescapeCommitLogText(entry.target_value);
    entry.previous_value = UnescapeCommitLogText(entry.previous_value);
  }
  return artifact->applied_count <= artifact->entries.size();
}

}  // namespace data_agent_system::agent_txn
