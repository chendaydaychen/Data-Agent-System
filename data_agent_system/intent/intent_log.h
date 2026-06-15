#pragma once

#include <cstddef>
#include <vector>

#include "data_agent_system/intent/intent.h"

namespace data_agent_system::intent {

class IntentLog {
 public:
  void Append(const WriteIntent& intent) { intents_.push_back(intent); }

  void Truncate(std::size_t size) {
    if (size < intents_.size()) {
      intents_.resize(size);
    }
  }

  const std::vector<WriteIntent>& Entries() const { return intents_; }

  std::size_t Size() const { return intents_.size(); }

 private:
  std::vector<WriteIntent> intents_;
};

}  // namespace data_agent_system::intent
