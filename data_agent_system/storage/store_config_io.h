#pragma once

#include <fstream>
#include <string>

#include "data_agent_system/storage/store_factory.h"

namespace data_agent_system::storage {

inline const char* kStoreConfigHeader = "DAS_STORE_CONFIG_V1";

inline std::string EscapeStoreConfigText(const std::string& input) {
  std::string output;
  output.reserve(input.size());
  for (const char ch : input) {
    if (ch == '\\' || ch == '\t' || ch == '\n') {
      output.push_back('\\');
      switch (ch) {
        case '\\':
          output.push_back('\\');
          break;
        case '\t':
          output.push_back('t');
          break;
        case '\n':
          output.push_back('n');
          break;
      }
    } else {
      output.push_back(ch);
    }
  }
  return output;
}

inline bool WriteStoreConfigArtifact(const StoreConfig& config, const std::string& output_path) {
  std::ofstream output(output_path.c_str(), std::ios::trunc);
  if (!output.is_open()) {
    return false;
  }

  output << kStoreConfigHeader << '\n';
  output << "kind=" << StoreKindName(config.kind) << '\n';
  output << "path=" << EscapeStoreConfigText(config.path) << '\n';
  output << "namespace_prefix=" << EscapeStoreConfigText(config.namespace_prefix) << '\n';
  output << "host=" << EscapeStoreConfigText(config.host) << '\n';
  output << "port=" << config.port << '\n';
  output << "database_index=" << config.database_index << '\n';
  output << "column_family=" << EscapeStoreConfigText(config.column_family) << '\n';
  output.flush();
  return output.good();
}

}  // namespace data_agent_system::storage
