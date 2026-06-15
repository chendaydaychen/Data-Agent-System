#pragma once

#include <string>

#include "data_agent_system/intent/intent_type.h"

namespace data_agent_system::intent {

enum class ConditionType {
  kNone,
  kValueEquals,
};

struct Condition {
  ConditionType type = ConditionType::kNone;
  std::string expected_value;
};

struct WriteIntent {
  std::string object_id;
  IntentType intent_type = IntentType::kRead;
  std::string payload;
  Condition condition;
};

}  // namespace data_agent_system::intent
