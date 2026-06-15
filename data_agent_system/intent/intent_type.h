#pragma once

namespace data_agent_system::intent {

enum class IntentType {
  kRead,
  kOverwrite,
  kAppend,
  kDelta,
  kCas,
};

}  // namespace data_agent_system::intent
