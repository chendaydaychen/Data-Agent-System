#pragma once

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "data_agent_system/cache/object_cache.h"

namespace data_agent_system::cache {

class WriteBuffer {
 public:
  void Upsert(const ObjectCacheEntry& entry) {
    const auto it = entries_.find(entry.object_id);

    ChangeRecord change;
    change.object_id = entry.object_id;
    change.had_previous = (it != entries_.end());
    if (change.had_previous) {
      change.previous_entry = it->second;
    } else {
      order_.push_back(entry.object_id);
    }
    changes_.push_back(change);
    entries_[entry.object_id] = entry;
  }

  std::optional<ObjectCacheEntry> Find(const std::string& object_id) const {
    const auto it = entries_.find(object_id);
    if (it == entries_.end()) {
      return std::nullopt;
    }
    return it->second;
  }

  std::vector<ObjectCacheEntry> Entries() const {
    std::vector<ObjectCacheEntry> ordered_entries;
    ordered_entries.reserve(order_.size());
    for (const auto& object_id : order_) {
      ordered_entries.push_back(entries_.at(object_id));
    }
    return ordered_entries;
  }

  void Truncate(std::size_t size) {
    while (changes_.size() > size) {
      const auto change = changes_.back();
      changes_.pop_back();
      if (change.had_previous) {
        entries_[change.object_id] = change.previous_entry;
        continue;
      }

      entries_.erase(change.object_id);
      const auto order_it = std::find(order_.begin(), order_.end(), change.object_id);
      if (order_it != order_.end()) {
        order_.erase(order_it);
      }
    }
  }

  std::size_t Size() const { return changes_.size(); }

 private:
  struct ChangeRecord {
    std::string object_id;
    bool had_previous = false;
    ObjectCacheEntry previous_entry;
  };

  std::vector<std::string> order_;
  std::unordered_map<std::string, ObjectCacheEntry> entries_;
  std::vector<ChangeRecord> changes_;
};

}  // namespace data_agent_system::cache
