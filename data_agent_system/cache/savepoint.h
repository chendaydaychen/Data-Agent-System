#pragma once

#include <cstddef>
#include <string>

namespace data_agent_system::cache {

struct Savepoint {
  std::string savepoint_id;
  std::size_t write_log_position = 0;
  std::size_t intent_log_position = 0;
};

}  // namespace data_agent_system::cache
