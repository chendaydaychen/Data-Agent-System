#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace data_agent_system::cache {

struct ReadRecord {
  std::string object_id;
  std::uint64_t observed_version = 0;
};

class ReadSet {
 public:
  void Record(const std::string& object_id, std::uint64_t observed_version) {
    ReadRecord record;
    record.object_id = object_id;
    record.observed_version = observed_version;
    entries_.push_back(record);
  }

  const std::vector<ReadRecord>& Entries() const { return entries_; }

 private:
  std::vector<ReadRecord> entries_;
};

}  // namespace data_agent_system::cache
