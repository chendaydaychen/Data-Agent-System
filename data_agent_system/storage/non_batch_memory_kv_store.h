#pragma once

#include <string>
#include <vector>

#include "data_agent_system/storage/memory_kv_store.h"

namespace data_agent_system::storage {

class NonBatchMemoryKVStore : public VersionedKVStore {
 public:
  bool SupportsAtomicBatchConditionalWrite() const override { return false; }

  VersionedValue Get(const std::string& key) const override { return fallback_.Get(key); }

  std::uint64_t GetVersion(const std::string& key) const override {
    return fallback_.GetVersion(key);
  }

  bool Put(const std::string& key, const std::string& value) override {
    return fallback_.Put(key, value);
  }

  bool PutIfVersion(const std::string& key,
                    std::uint64_t expected_version,
                    const std::string& value) override {
    return fallback_.PutIfVersion(key, expected_version, value);
  }

  bool DeleteIfVersion(const std::string& key,
                       std::uint64_t expected_version) override {
    return fallback_.DeleteIfVersion(key, expected_version);
  }

  bool BatchPutIfVersion(const std::vector<VersionCheck>&,
                         const std::vector<WriteOp>&) override {
    return false;
  }

 private:
  MemoryKVStore fallback_;
};

}  // namespace data_agent_system::storage
