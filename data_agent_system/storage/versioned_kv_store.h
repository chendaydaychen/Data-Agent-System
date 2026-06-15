#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace data_agent_system::storage {

struct VersionedValue {
  std::string value;
  std::uint64_t version = 0;
  bool exists = false;
};

struct VersionCheck {
  std::string key;
  std::uint64_t expected_version = 0;
};

struct WriteOp {
  std::string key;
  std::string value;
};

class VersionedKVStore {
 public:
  virtual ~VersionedKVStore() = default;

  virtual bool SupportsAtomicBatchConditionalWrite() const { return true; }
  virtual VersionedValue Get(const std::string& key) const = 0;
  virtual std::uint64_t GetVersion(const std::string& key) const = 0;
  virtual bool Put(const std::string& key, const std::string& value) = 0;
  virtual bool PutIfVersion(const std::string& key,
                            std::uint64_t expected_version,
                            const std::string& value) = 0;
  virtual bool DeleteIfVersion(const std::string& key,
                               std::uint64_t expected_version) = 0;
  virtual bool BatchPutIfVersion(const std::vector<VersionCheck>& checks,
                                 const std::vector<WriteOp>& writes) = 0;
};

}  // namespace data_agent_system::storage
