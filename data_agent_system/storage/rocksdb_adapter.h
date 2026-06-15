#pragma once

#include <sys/stat.h>

#include <fstream>
#include <string>
#include <vector>

#include "data_agent_system/storage/file_kv_store.h"

namespace data_agent_system::storage {

class RocksDbAdapter : public VersionedKVStore {
 public:
  explicit RocksDbAdapter(std::string path, std::string column_family = "default")
      : root_path_(path.empty() ? "/tmp/data_agent_system_rocksdb" : path),
        column_family_(std::move(column_family)),
        fallback_store_(StoreFilePath(root_path_)) {
    EnsureDirectory(root_path_);
    WriteManifest();
  }

  // Placeholder compatibility adapter until a real RocksDB binding is linked in.
  // It uses a RocksDB-like directory root with manifest/current files on top of FileKVStore.
  VersionedValue Get(const std::string& key) const override {
    return fallback_store_.Get(key);
  }

  std::uint64_t GetVersion(const std::string& key) const override {
    return fallback_store_.GetVersion(key);
  }

  bool Put(const std::string& key, const std::string& value) override {
    EnsureDirectory(root_path_);
    WriteManifest();
    return fallback_store_.Put(key, value);
  }

  bool PutIfVersion(const std::string& key,
                    std::uint64_t expected_version,
                    const std::string& value) override {
    EnsureDirectory(root_path_);
    WriteManifest();
    return fallback_store_.PutIfVersion(key, expected_version, value);
  }

  bool DeleteIfVersion(const std::string& key,
                       std::uint64_t expected_version) override {
    EnsureDirectory(root_path_);
    WriteManifest();
    return fallback_store_.DeleteIfVersion(key, expected_version);
  }

  bool BatchPutIfVersion(const std::vector<VersionCheck>& checks,
                         const std::vector<WriteOp>& writes) override {
    EnsureDirectory(root_path_);
    WriteManifest();
    return fallback_store_.BatchPutIfVersion(checks, writes);
  }

  const FileKVStore& FallbackStore() const { return fallback_store_; }
  const std::string& RootPath() const { return root_path_; }
  const std::string& ColumnFamily() const { return column_family_; }
  std::string ManifestPath() const { return root_path_ + "/MANIFEST.txt"; }
  std::string CurrentFilePath() const { return StoreFilePath(root_path_); }

 private:
  static std::string StoreFilePath(const std::string& root_path) {
    return root_path + "/CURRENT.tsv";
  }

  static void EnsureDirectory(const std::string& root_path) {
    ::mkdir(root_path.c_str(), 0755);
  }

  void WriteManifest() const {
    std::ofstream output(ManifestPath().c_str(), std::ios::trunc);
    if (!output.is_open()) {
      return;
    }
    output << "engine=rocksdb_compat\n";
    output << "column_family=" << column_family_ << '\n';
    output << "current=" << CurrentFilePath() << '\n';
    output.flush();
  }

  std::string root_path_;
  std::string column_family_;
  FileKVStore fallback_store_;
};

}  // namespace data_agent_system::storage
