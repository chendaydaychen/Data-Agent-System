#pragma once

#include <charconv>
#include <optional>
#include <string>

#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"

namespace data_agent_system::intent {

class PolicyDispatcher {
 public:
  static bool UsesSemanticRebase(const IntentType intent_type) {
    switch (intent_type) {
      case IntentType::kAppend:
      case IntentType::kDelta:
      case IntentType::kCas:
        return true;
      case IntentType::kRead:
      case IntentType::kOverwrite:
        return false;
    }
    return false;
  }

  static std::optional<std::string> ResolveValue(
      const data_agent_system::cache::ObjectCacheEntry& entry,
      const WriteIntent& intent,
      const std::string& current_store_value) {
    switch (intent.intent_type) {
      case IntentType::kRead:
        return std::nullopt;
      case IntentType::kOverwrite:
        return entry.current_value;
      case IntentType::kAppend: {
        if (entry.current_value.size() >= entry.base_value.size() &&
            entry.current_value.compare(0, entry.base_value.size(), entry.base_value) == 0) {
          return current_store_value + entry.current_value.substr(entry.base_value.size());
        }
        return current_store_value + intent.payload;
      }
      case IntentType::kDelta: {
        const auto current = ParseInt(current_store_value);
        const auto base = ParseInt(entry.base_value);
        const auto branch_value = ParseInt(entry.current_value);
        if (current.has_value() && base.has_value() && branch_value.has_value()) {
          return std::to_string(*current + (*branch_value - *base));
        }
        const auto delta = ParseInt(intent.payload);
        if (!current.has_value() || !delta.has_value()) {
          return std::nullopt;
        }
        return std::to_string(*current + *delta);
      }
      case IntentType::kCas:
        if (intent.condition.type == ConditionType::kValueEquals &&
            current_store_value != intent.condition.expected_value) {
          return std::nullopt;
        }
        return entry.current_value;
    }
    return std::nullopt;
  }

 private:
  static std::optional<long long> ParseInt(const std::string& text) {
    long long value = 0;
    const auto* begin = text.data();
    const auto* end = text.data() + text.size();
    const auto result = std::from_chars(begin, end, value);
    if (result.ec != std::errc() || result.ptr != end) {
      return std::nullopt;
    }
    return value;
  }
};

}  // namespace data_agent_system::intent
