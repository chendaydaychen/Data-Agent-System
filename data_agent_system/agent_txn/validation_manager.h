#pragma once

#include <unordered_map>

#include "data_agent_system/agent_txn/agent_txn_context.h"
#include "data_agent_system/branch/branch_context.h"
#include "data_agent_system/intent/policy_dispatcher.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::agent_txn {

class ValidationManager {
 public:
  ValidationResult ValidateWinner(
      const data_agent_system::branch::BranchContext& winner,
      const data_agent_system::storage::VersionedKVStore& store) const {
    std::unordered_map<std::string, data_agent_system::intent::IntentType> latest_intents;
    for (const auto& intent : winner.intent_log.Entries()) {
      latest_intents[intent.object_id] = intent.intent_type;
    }

    for (const auto& read : winner.read_set.Entries()) {
      const auto intent_it = latest_intents.find(read.object_id);
      if (intent_it != latest_intents.end() &&
          data_agent_system::intent::PolicyDispatcher::UsesSemanticRebase(
              intent_it->second)) {
        continue;
      }
      if (store.GetVersion(read.object_id) != read.observed_version) {
        ValidationResult result;
        result.success = false;
        result.reason = "read version changed for " + read.object_id;
        return result;
      }
    }
    ValidationResult result;
    result.success = true;
    return result;
  }
};

}  // namespace data_agent_system::agent_txn
