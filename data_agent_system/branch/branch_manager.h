#pragma once

#include <algorithm>
#include <charconv>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "data_agent_system/branch/branch_context.h"
#include "data_agent_system/cache/object_cache.h"
#include "data_agent_system/intent/intent.h"
#include "data_agent_system/storage/versioned_kv_store.h"

namespace data_agent_system::branch {

class BranchManager {
 public:
  BranchContext& CreateBranch(std::vector<BranchContext>& branches,
                              const std::string& branch_id) const {
    BranchContext branch;
    branch.branch_id = branch_id;
    branch.status = BranchStatus::kCreated;
    branches.push_back(branch);
    return branches.back();
  }

  BranchContext* FindBranch(std::vector<BranchContext>& branches,
                            const std::string& branch_id) const {
    auto it = std::find_if(branches.begin(), branches.end(), [&](const BranchContext& branch) {
      return branch.branch_id == branch_id;
    });
    return it == branches.end() ? nullptr : &(*it);
  }

  const BranchContext* FindBranch(const std::vector<BranchContext>& branches,
                                  const std::string& branch_id) const {
    auto it = std::find_if(branches.begin(), branches.end(), [&](const BranchContext& branch) {
      return branch.branch_id == branch_id;
    });
    return it == branches.end() ? nullptr : &(*it);
  }

  std::string SelectWinner(const std::vector<BranchContext>& branches) const {
    if (branches.empty()) {
      throw std::runtime_error("cannot select winner from empty branch list");
    }
    const auto winner = std::max_element(
        branches.begin(), branches.end(), [](const BranchContext& lhs, const BranchContext& rhs) {
          return lhs.candidate_result.score < rhs.candidate_result.score;
        });
    return winner->branch_id;
  }

  void MarkWinnerAndLosers(std::vector<BranchContext>& branches,
                           const std::string& winner_branch_id) const {
    for (auto& branch : branches) {
      if (branch.branch_id == winner_branch_id) {
        branch.status = BranchStatus::kWinner;
      } else {
        branch.status = BranchStatus::kLoser;
      }
    }
  }

  ReadResult ReadObject(BranchContext& branch,
                        const std::string& object_id,
                        const data_agent_system::storage::VersionedKVStore& store) const {
    branch.status = BranchStatus::kRunning;

    const auto buffered = branch.write_buffer.Find(object_id);
    if (buffered.has_value()) {
      ReadResult result;
      result.value = buffered->current_value;
      result.version = buffered->base_version;
      result.exists = buffered->undo_record.existed || !buffered->base_value.empty() ||
                      !buffered->current_value.empty();
      return result;
    }

    const auto value = store.Get(object_id);
    branch.read_set.Record(object_id, value.version);
    ReadResult result;
    result.value = value.value;
    result.version = value.version;
    result.exists = value.exists;
    return result;
  }

  data_agent_system::cache::Savepoint CreateSavepoint(BranchContext& branch,
                                                      const std::string& savepoint_id) const {
    data_agent_system::cache::Savepoint savepoint;
    savepoint.savepoint_id = savepoint_id;
    savepoint.write_log_position = branch.write_buffer.Size();
    savepoint.intent_log_position = branch.intent_log.Size();
    branch.savepoints.push_back(savepoint);
    return savepoint;
  }

  void StageBranch(BranchContext& branch, double score, const std::string& summary) const {
    branch.candidate_result.score = score;
    branch.candidate_result.summary = summary;
    branch.status = BranchStatus::kStaged;
  }

  void BufferWrite(BranchContext& branch,
                   const std::string& object_id,
                   data_agent_system::cache::ObjectType object_type,
                   const data_agent_system::intent::WriteIntent& intent,
                   const data_agent_system::storage::VersionedKVStore& store) const {
    branch.status = BranchStatus::kRunning;

    data_agent_system::cache::ObjectCacheEntry entry;
    const auto existing = branch.write_buffer.Find(object_id);
    if (existing.has_value()) {
      entry = *existing;
    } else {
      const auto base = store.Get(object_id);
      entry.object_id = object_id;
      entry.object_type = object_type;
      entry.base_value = base.value;
      entry.base_version = base.version;
      entry.current_value = base.value;
      entry.undo_record.previous_value = base.value;
      entry.undo_record.existed = base.exists;
    }

    entry.object_type = object_type;
    entry.dirty = (intent.intent_type != data_agent_system::intent::IntentType::kRead);
    entry.intent_type = intent.intent_type;

    switch (intent.intent_type) {
      case data_agent_system::intent::IntentType::kRead:
        entry.current_value = entry.base_value;
        break;
      case data_agent_system::intent::IntentType::kOverwrite:
      case data_agent_system::intent::IntentType::kCas:
        entry.current_value = intent.payload;
        break;
      case data_agent_system::intent::IntentType::kAppend:
        entry.current_value = entry.current_value + intent.payload;
        break;
      case data_agent_system::intent::IntentType::kDelta: {
        const auto current = ParseInt(entry.current_value);
        const auto delta = ParseInt(intent.payload);
        if (current.has_value() && delta.has_value()) {
          entry.current_value = std::to_string(*current + *delta);
        }
        break;
      }
    }

    branch.write_buffer.Upsert(entry);
    branch.intent_log.Append(intent);
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

}  // namespace data_agent_system::branch
