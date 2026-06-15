#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "data_agent_system/branch/branch_result.h"
#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/cache/read_set.h"
#include "data_agent_system/cache/savepoint.h"
#include "data_agent_system/cache/write_buffer.h"
#include "data_agent_system/intent/intent_log.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::branch {

enum class BranchStatus {
  kCreated,
  kRunning,
  kStaged,
  kWinner,
  kLoser,
  kCommitted,
  kDiscarded,
  kAborted,
};

struct BranchContext {
  std::string branch_id;
  BranchStatus status = BranchStatus::kCreated;
  data_agent_system::cache::ReadSet read_set;
  data_agent_system::cache::WriteBuffer write_buffer;
  data_agent_system::intent::IntentLog intent_log;
  CandidateResult candidate_result;
  std::vector<data_agent_system::cache::Savepoint> savepoints;
};

struct ReadResult {
  std::string value;
  std::uint64_t version = 0;
  bool exists = false;
};

}  // namespace data_agent_system::branch
