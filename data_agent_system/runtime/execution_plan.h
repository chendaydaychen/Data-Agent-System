#pragma once

#include <unordered_set>
#include <stdexcept>
#include <string>
#include <vector>

namespace data_agent_system::runtime {

enum class WinnerSelectionPolicy {
  kHighestScore,
};

struct BranchPlan {
  std::string branch_id;
  std::string candidate_id;
  std::string rationale;
  double initial_priority = 0.0;
};

struct ExecutionPlan {
  WinnerSelectionPolicy winner_selection_policy = WinnerSelectionPolicy::kHighestScore;
  std::vector<BranchPlan> branch_plans;

  void AddBranchPlan(const std::string& branch_id,
                     const std::string& candidate_id,
                     const std::string& rationale,
                     double initial_priority = 0.0) {
    branch_plans.push_back(BranchPlan{branch_id, candidate_id, rationale, initial_priority});
  }

  std::vector<std::string> CandidateBranchIds() const {
    std::vector<std::string> branch_ids;
    branch_ids.reserve(branch_plans.size());
    for (const auto& plan : branch_plans) {
      branch_ids.push_back(plan.branch_id);
    }
    return branch_ids;
  }

  void Validate() const {
    if (branch_plans.empty()) {
      throw std::runtime_error("execution plan must contain at least one branch");
    }
    std::unordered_set<std::string> branch_ids;
    for (const auto& branch_plan : branch_plans) {
      if (branch_plan.branch_id.empty()) {
        throw std::runtime_error("execution plan contains an empty branch_id");
      }
      if (branch_plan.candidate_id.empty()) {
        throw std::runtime_error("execution plan contains an empty candidate_id");
      }
      if (!branch_ids.insert(branch_plan.branch_id).second) {
        throw std::runtime_error("execution plan contains duplicate branch_id: " +
                                 branch_plan.branch_id);
      }
    }
  }
};

}  // namespace data_agent_system::runtime
