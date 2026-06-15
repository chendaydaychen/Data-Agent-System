#pragma once

#include <cstdlib>
#include <cstdint>
#include <fstream>
#include <mutex>
#include <cstdio>
#include <sstream>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::storage {

class FileKVStore : public VersionedKVStore {
 public:
  explicit FileKVStore(std::string path) : path_(std::move(path)) { LoadFromDisk(); }

  VersionedValue Get(const std::string& key) const override {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = values_.find(key);
    if (it == values_.end()) {
      return {};
    }
    VersionedValue result;
    result.value = it->second.value;
    result.version = it->second.version;
    result.exists = true;
    return result;
  }

  std::uint64_t GetVersion(const std::string& key) const override { return Get(key).version; }

  bool Put(const std::string& key, const std::string& value) override {
    std::lock_guard<std::mutex> lock(mu_);
    auto& entry = values_[key];
    entry.value = value;
    entry.version += 1;
    return PersistToDisk();
  }

  bool PutIfVersion(const std::string& key,
                    std::uint64_t expected_version,
                    const std::string& value) override {
    std::lock_guard<std::mutex> lock(mu_);
    auto& entry = values_[key];
    if (entry.version != expected_version) {
      return false;
    }
    entry.value = value;
    entry.version += 1;
    return PersistToDisk();
  }

  bool DeleteIfVersion(const std::string& key,
                       std::uint64_t expected_version) override {
    std::lock_guard<std::mutex> lock(mu_);
    const auto it = values_.find(key);
    const std::uint64_t current_version = (it == values_.end()) ? 0 : it->second.version;
    if (current_version != expected_version) {
      return false;
    }
    if (it != values_.end()) {
      values_.erase(it);
    }
    return PersistToDisk();
  }

  bool BatchPutIfVersion(const std::vector<VersionCheck>& checks,
                         const std::vector<WriteOp>& writes) override {
    std::lock_guard<std::mutex> lock(mu_);
    for (const auto& check : checks) {
      const auto it = values_.find(check.key);
      const std::uint64_t current_version = (it == values_.end()) ? 0 : it->second.version;
      if (current_version != check.expected_version) {
        return false;
      }
    }

    auto next_values = values_;
    for (const auto& write : writes) {
      auto& entry = next_values[write.key];
      entry.value = write.value;
      entry.version += 1;
    }

    values_.swap(next_values);
    return PersistToDisk();
  }

  const std::string& Path() const { return path_; }
  std::string TempPath() const { return path_ + ".tmp"; }
  std::string JournalPath() const { return path_ + ".journal"; }

  bool Reload() {
    std::lock_guard<std::mutex> lock(mu_);
    return LoadFromDiskUnlocked();
  }

  std::vector<std::pair<std::string, VersionedValue>> Snapshot() const {
    std::lock_guard<std::mutex> lock(mu_);
    std::vector<std::pair<std::string, VersionedValue>> snapshot;
    snapshot.reserve(values_.size());
    for (const auto& pair : values_) {
      VersionedValue value;
      value.value = pair.second.value;
      value.version = pair.second.version;
      value.exists = true;
      snapshot.push_back(std::make_pair(pair.first, value));
    }
    return snapshot;
  }

 private:
  static constexpr const char* kFileHeader = "DAS_KV_V1";
  static constexpr const char* kJournalHeader = "DAS_KV_JOURNAL_V1";

  struct Entry {
    std::string value;
    std::uint64_t version = 0;
  };

  static std::string Escape(const std::string& input) {
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

  static std::string Unescape(const std::string& input) {
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

  void LoadFromDisk() {
    std::lock_guard<std::mutex> lock(mu_);
    LoadFromDiskUnlocked();
  }

  bool PersistToDisk() const {
    if (!WriteJournalUnlocked()) {
      return false;
    }

    const std::string temp_path = TempPath();
    if (!WriteSnapshotUnlocked(temp_path, values_)) {
      return false;
    }

    if (std::rename(temp_path.c_str(), path_.c_str()) != 0) {
      return false;
    }
    return std::remove(JournalPath().c_str()) == 0;
  }

  bool LoadFromDiskUnlocked() {
    values_.clear();
    if (!RecoverIfNeededUnlocked()) {
      return false;
    }

    std::ifstream input(path_.c_str());
    if (!input.is_open()) {
      return true;
    }

    std::string line;
    if (!std::getline(input, line)) {
      return true;
    }
    if (line != kFileHeader) {
      return false;
    }

    while (std::getline(input, line)) {
      if (line.empty()) {
        continue;
      }
      std::istringstream stream(line);
      std::string encoded_key;
      std::string version_text;
      std::string encoded_value;
      if (!std::getline(stream, encoded_key, '\t') || !std::getline(stream, version_text, '\t') ||
          !std::getline(stream, encoded_value)) {
        return false;
      }
      Entry entry;
      entry.version = static_cast<std::uint64_t>(std::strtoull(version_text.c_str(), nullptr, 10));
      entry.value = Unescape(encoded_value);
      values_[Unescape(encoded_key)] = entry;
    }
    return true;
  }

  bool RecoverIfNeededUnlocked() const {
    const bool has_journal = FileExistsUnlocked(JournalPath());
    const bool has_temp = FileExistsUnlocked(TempPath());

    if (has_journal && has_temp) {
      if (std::rename(TempPath().c_str(), path_.c_str()) != 0) {
        return false;
      }
      return std::remove(JournalPath().c_str()) == 0;
    }

    if (has_journal && !has_temp) {
      return std::remove(JournalPath().c_str()) == 0;
    }

    if (!has_journal && has_temp) {
      return std::remove(TempPath().c_str()) == 0;
    }

    return true;
  }

  bool WriteSnapshotUnlocked(const std::string& output_path,
                             const std::unordered_map<std::string, Entry>& values) const {
    std::ofstream output(output_path.c_str(), std::ios::trunc);
    if (!output.is_open()) {
      return false;
    }

    output << kFileHeader << '\n';
    for (const auto& pair : values) {
      output << Escape(pair.first) << '\t' << pair.second.version << '\t'
             << Escape(pair.second.value) << '\n';
    }
    output.flush();
    if (!output.good()) {
      return false;
    }
    output.close();
    return true;
  }

  bool WriteJournalUnlocked() const {
    std::ofstream output(JournalPath().c_str(), std::ios::trunc);
    if (!output.is_open()) {
      return false;
    }
    output << kJournalHeader << '\n';
    output << "target=" << Escape(path_) << '\n';
    output << "temp=" << Escape(TempPath()) << '\n';
    output << "mode=replace_with_temp\n";
    output.flush();
    if (!output.good()) {
      return false;
    }
    output.close();
    return true;
  }

  static bool FileExistsUnlocked(const std::string& file_path) {
    std::ifstream input(file_path.c_str());
    return input.good();
  }

  std::string path_;
  mutable std::mutex mu_;
  std::unordered_map<std::string, Entry> values_;
};

}  // namespace data_agent_system::storage
