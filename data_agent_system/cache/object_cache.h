#pragma once

#include <cstdint>
#include <string>

#include "data_agent_system/intent/intent_type.h"

namespace data_agent_system::cache {

enum class ObjectType {
  kGeneric,
  kRow,
  kText,
  kCandidateResult,
};

struct UndoRecord {
  std::string previous_value;
  bool existed = false;
};

struct ObjectCacheEntry {
  std::string object_id;
  ObjectType object_type = ObjectType::kGeneric;
  std::string base_value;
  std::uint64_t base_version = 0;
  std::string current_value;
  bool dirty = false;
  data_agent_system::intent::IntentType intent_type =
      data_agent_system::intent::IntentType::kRead;
  UndoRecord undo_record;
};

}  // namespace data_agent_system::cache
