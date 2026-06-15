#pragma once

#include <string>

namespace data_agent_system::branch {

struct CandidateResult {
  double score = 0.0;
  std::string summary;
};

}  // namespace data_agent_system::branch
