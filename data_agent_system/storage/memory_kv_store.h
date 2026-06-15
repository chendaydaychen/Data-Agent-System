#pragma once

#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::storage {

class MemoryKVStore : public VersionedKVStore {
 public:
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

  std::uint64_t GetVersion(const std::string& key) const override {
    return Get(key).version;
  }

  bool Put(const std::string& key, const std::string& value) override {
    std::lock_guard<std::mutex> lock(mu_);
    auto& entry = values_[key];
    entry.value = value;
    entry.version += 1;
    return true;
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
    return true;
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

    for (const auto& write : writes) {
      auto& entry = values_[write.key];
      entry.value = write.value;
      entry.version += 1;
    }
    return true;
  }

 private:
  struct Entry {
    std::string value;
    std::uint64_t version = 0;
  };

  mutable std::mutex mu_;
  std::unordered_map<std::string, Entry> values_;
};

}  // namespace data_agent_system::storage
