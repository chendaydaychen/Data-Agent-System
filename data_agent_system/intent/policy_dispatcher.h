#pragma once

#include <charconv>
#include <optional>
#include <string>

#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"

namespace data_agent_system::intent {

class PolicyDispatcher {
 public:
  enum class ConcurrencyClass {
    kReadOnly,
    kStrict,
    kCommutativeRebase,
    kConditionalRebase,
  };

  struct ResolveResult {
    bool success = true;
    bool should_write = false;
    std::string value;
    std::string reason;
  };

  static ConcurrencyClass Classify(const IntentType intent_type) {
    switch (intent_type) {
      case IntentType::kRead:
        return ConcurrencyClass::kReadOnly;
      case IntentType::kOverwrite:
        return ConcurrencyClass::kStrict;
      case IntentType::kAppend:
      case IntentType::kDelta:
        return ConcurrencyClass::kCommutativeRebase;
      case IntentType::kCas:
        return ConcurrencyClass::kConditionalRebase;
    }
    return ConcurrencyClass::kStrict;
  }

  static bool UsesSemanticRebase(const IntentType intent_type) {
    return Classify(intent_type) != ConcurrencyClass::kStrict &&
           Classify(intent_type) != ConcurrencyClass::kReadOnly;
  }

  static const char* ConcurrencyClassName(const IntentType intent_type) {
    switch (Classify(intent_type)) {
      case ConcurrencyClass::kReadOnly:
        return "read_only";
      case ConcurrencyClass::kStrict:
        return "strict";
      case ConcurrencyClass::kCommutativeRebase:
        return "commutative_rebase";
      case ConcurrencyClass::kConditionalRebase:
        return "conditional_rebase";
    }
    return "strict";
  }

  static ResolveResult ResolveWrite(
      const data_agent_system::cache::ObjectCacheEntry& entry,
      const WriteIntent& intent,
      const std::string& current_store_value) {
    ResolveResult result;
    switch (intent.intent_type) {
      case IntentType::kRead:
        result.should_write = false;
        return result;
      case IntentType::kOverwrite:
        result.should_write = true;
        result.value = entry.current_value;
        return result;
      case IntentType::kAppend: {
        result.should_write = true;
        if (entry.current_value.size() >= entry.base_value.size() &&
            entry.current_value.compare(0, entry.base_value.size(), entry.base_value) == 0) {
          result.value = current_store_value + entry.current_value.substr(entry.base_value.size());
          return result;
        }
        result.value = current_store_value + intent.payload;
        return result;
      }
      case IntentType::kDelta: {
        result.should_write = true;
        const auto current = ParseInt(current_store_value);
        const auto base = ParseInt(entry.base_value);
        const auto branch_value = ParseInt(entry.current_value);
        if (current.has_value() && base.has_value() && branch_value.has_value()) {
          result.value = std::to_string(*current + (*branch_value - *base));
          return result;
        }
        const auto delta = ParseInt(intent.payload);
        if (!current.has_value() || !delta.has_value()) {
          result.success = false;
          result.reason = "delta rebase requires integer base/current values";
          return result;
        }
        result.value = std::to_string(*current + *delta);
        return result;
      }
      case IntentType::kCas:
        if (intent.condition.type == ConditionType::kValueEquals &&
            current_store_value != intent.condition.expected_value) {
          result.success = false;
          result.reason = "cas condition no longer holds";
          return result;
        }
        result.should_write = true;
        result.value = entry.current_value;
        return result;
    }
    result.success = false;
    result.reason = "unknown intent type";
    return result;
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
