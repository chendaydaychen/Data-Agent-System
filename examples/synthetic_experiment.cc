#include <cstdlib>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#include "data_agent_system/runtime/task_runtime.h"
#include "data_agent_system/storage/store_factory.h"
#include "data_agent_system/storage/store_config_io.h"
#include "data_agent_system/workloads/synthetic/synthetic_driver.h"

int main(int argc, char** argv) {
  data_agent_system::workloads::synthetic::SyntheticRunConfig config;
  std::string output_path;
  std::string backend = "memory";
  std::string store_path;
  std::string commit_log_dir;
  std::string namespace_prefix;
  std::string host = "127.0.0.1";
  std::uint16_t port = 6379;
  std::int32_t database_index = 0;
  std::string column_family = "default";
  std::string store_config_path;
  std::string task_event_log_dir;

  if (argc > 1) {
    config.task_count = static_cast<std::size_t>(std::strtoull(argv[1], nullptr, 10));
  }
  if (argc > 2) {
    config.branch_count = static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
  }
  if (argc > 3) {
    config.conflict_every = static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10));
  }
  if (argc > 4) {
    output_path = argv[4];
  }
  if (argc > 5) {
    backend = argv[5];
  }
  if (argc > 6) {
    store_path = argv[6];
  }
  if (argc > 7) {
    commit_log_dir = argv[7];
  }
  if (argc > 8) {
    namespace_prefix = argv[8];
  }
  if (argc > 9) {
    host = argv[9];
  }
  if (argc > 10) {
    port = static_cast<std::uint16_t>(std::strtoul(argv[10], nullptr, 10));
  }
  if (argc > 11) {
    database_index = static_cast<std::int32_t>(std::strtol(argv[11], nullptr, 10));
  }
  if (argc > 12) {
    column_family = argv[12];
  }
  if (argc > 13) {
    store_config_path = argv[13];
  }
  if (argc > 14) {
    task_event_log_dir = argv[14];
  }
  config.commit_log_dir = commit_log_dir;
  config.task_event_log_dir = task_event_log_dir;

  data_agent_system::storage::StoreKind store_kind;
  if (!data_agent_system::storage::ParseStoreKind(backend, &store_kind)) {
    std::cerr << "unsupported backend name: " << backend << "\n";
    return 1;
  }

  data_agent_system::storage::StoreConfig store_config;
  store_config.kind = store_kind;
  store_config.path = store_path;
  store_config.namespace_prefix = namespace_prefix;
  store_config.host = host;
  store_config.port = port;
  store_config.database_index = database_index;
  store_config.column_family = column_family;

  std::string error;
  std::unique_ptr<data_agent_system::storage::VersionedKVStore> store =
      data_agent_system::storage::CreateStore(store_config, &error);
  if (!store) {
    std::cerr << "failed to create store: " << error << "\n";
    return 1;
  }

  if (!store_config_path.empty() &&
      !data_agent_system::storage::WriteStoreConfigArtifact(store_config, store_config_path)) {
    std::cerr << "failed to write store config artifact: " << store_config_path << "\n";
    return 1;
  }

  data_agent_system::runtime::TaskRuntime runtime(*store);
  data_agent_system::workloads::synthetic::SyntheticRunResult result;
  result =
      data_agent_system::workloads::synthetic::RunSyntheticExperimentWithStore(
          config, runtime, *store);

  if (output_path.empty()) {
    data_agent_system::workloads::synthetic::WriteSyntheticCsv(result, std::cout);
    return 0;
  }

  std::ofstream output(output_path.c_str());
  if (!output.is_open()) {
    std::cerr << "failed to open output path: " << output_path << "\n";
    return 1;
  }
  data_agent_system::workloads::synthetic::WriteSyntheticCsv(result, output);
  return 0;
}
