#pragma once

#include <string>
#include <vector>

#include "data_agent_system/storage/memory_kv_store.h"

namespace data_agent_system::storage {

class RedisAdapter : public VersionedKVStore {
 public:
  explicit RedisAdapter(std::string host = "127.0.0.1",
                        std::uint16_t port = 6379,
                        std::int32_t database_index = 0,
                        std::string namespace_prefix = "")
      : host_(std::move(host)),
        port_(port),
        database_index_(database_index),
        namespace_prefix_(std::move(namespace_prefix)) {}

  // Placeholder compatibility adapter until a real Redis client is linked in.
  VersionedValue Get(const std::string& key) const override {
    return fallback_store_.Get(NamespacedKey(key));
  }

  std::uint64_t GetVersion(const std::string& key) const override {
    return fallback_store_.GetVersion(NamespacedKey(key));
  }

  bool Put(const std::string& key, const std::string& value) override {
    return fallback_store_.Put(NamespacedKey(key), value);
  }

  bool PutIfVersion(const std::string& key,
                    std::uint64_t expected_version,
                    const std::string& value) override {
    return fallback_store_.PutIfVersion(NamespacedKey(key), expected_version, value);
  }

  bool BatchPutIfVersion(const std::vector<VersionCheck>& checks,
                         const std::vector<WriteOp>& writes) override {
    std::vector<VersionCheck> namespaced_checks;
    namespaced_checks.reserve(checks.size());
    for (const auto& check : checks) {
      VersionCheck namespaced_check;
      namespaced_check.key = NamespacedKey(check.key);
      namespaced_check.expected_version = check.expected_version;
      namespaced_checks.push_back(namespaced_check);
    }

    std::vector<WriteOp> namespaced_writes;
    namespaced_writes.reserve(writes.size());
    for (const auto& write : writes) {
      WriteOp namespaced_write;
      namespaced_write.key = NamespacedKey(write.key);
      namespaced_write.value = write.value;
      namespaced_writes.push_back(namespaced_write);
    }
    return fallback_store_.BatchPutIfVersion(namespaced_checks, namespaced_writes);
  }

  const MemoryKVStore& FallbackStore() const { return fallback_store_; }
  const std::string& Host() const { return host_; }
  std::uint16_t Port() const { return port_; }
  std::int32_t DatabaseIndex() const { return database_index_; }
  const std::string& NamespacePrefix() const { return namespace_prefix_; }

 private:
  std::string NamespacedKey(const std::string& key) const {
    std::string prefix = "db" + std::to_string(database_index_);
    if (!namespace_prefix_.empty()) {
      prefix += ":" + namespace_prefix_;
    }
    return prefix + ":" + key;
  }

  MemoryKVStore fallback_store_;
  std::string host_;
  std::uint16_t port_;
  std::int32_t database_index_;
  std::string namespace_prefix_;
};

}  // namespace data_agent_system::storage
