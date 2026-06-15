#pragma once

#include <memory>
#include <string>

#include "data_agent_system/storage/file_kv_store.h"
#include "data_agent_system/storage/memory_kv_store.h"
#include "data_agent_system/storage/redis_adapter.h"
#include "data_agent_system/storage/rocksdb_adapter.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::storage {

enum class StoreKind {
  kMemory,
  kFile,
  kRocksDb,
  kRedis,
};

struct StoreConfig {
  StoreKind kind = StoreKind::kMemory;
  std::string path;
  std::string namespace_prefix;
  std::string host = "127.0.0.1";
  std::uint16_t port = 0;
  std::int32_t database_index = 0;
  std::string column_family = "default";
};

inline const char* StoreKindName(StoreKind kind) {
  switch (kind) {
    case StoreKind::kMemory:
      return "memory";
    case StoreKind::kFile:
      return "file";
    case StoreKind::kRocksDb:
      return "rocksdb";
    case StoreKind::kRedis:
      return "redis";
  }
  return "unknown";
}

inline bool ParseStoreKind(const std::string& name, StoreKind* kind) {
  if (kind == nullptr) {
    return false;
  }
  if (name == "memory") {
    *kind = StoreKind::kMemory;
    return true;
  }
  if (name == "file") {
    *kind = StoreKind::kFile;
    return true;
  }
  if (name == "rocksdb") {
    *kind = StoreKind::kRocksDb;
    return true;
  }
  if (name == "redis") {
    *kind = StoreKind::kRedis;
    return true;
  }
  return false;
}

inline std::unique_ptr<VersionedKVStore> CreateStore(
    const StoreConfig& config,
    std::string* error) {
  switch (config.kind) {
    case StoreKind::kMemory:
      return std::unique_ptr<VersionedKVStore>(new MemoryKVStore());
    case StoreKind::kFile: {
      const std::string path =
          config.path.empty() ? "/tmp/data_agent_system_store.tsv" : config.path;
      return std::unique_ptr<VersionedKVStore>(new FileKVStore(path));
    }
    case StoreKind::kRocksDb: {
      const std::string path =
          config.path.empty() ? "/tmp/data_agent_system_rocksdb" : config.path;
      return std::unique_ptr<VersionedKVStore>(
          new RocksDbAdapter(path, config.column_family));
    }
    case StoreKind::kRedis:
      return std::unique_ptr<VersionedKVStore>(
          new RedisAdapter(config.host, config.port, config.database_index, config.namespace_prefix));
  }
  if (error != nullptr) {
    *error = "unknown store kind";
  }
  return nullptr;
}

}  // namespace data_agent_system::storage
