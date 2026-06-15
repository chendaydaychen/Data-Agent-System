#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TESTDATA_DIR="${ROOT_DIR}/data_agent_system/experiments/testdata"
CSV_PATH="${TESTDATA_DIR}/synthetic_results_sample.csv"
STORE_PATH="${TESTDATA_DIR}/synthetic_store_sample.tsv"
SNAPSHOT_PATH="${TESTDATA_DIR}/synthetic_store_reload_expected.csv"
RECOVERY_STORE_PATH="${TESTDATA_DIR}/synthetic_store_recovery_base.tsv"
RECOVERY_TEMP_PATH="${TESTDATA_DIR}/synthetic_store_recovery_base.tsv.tmp"
RECOVERY_JOURNAL_PATH="${TESTDATA_DIR}/synthetic_store_recovery_base.tsv.journal"
RECOVERY_EXPECTED_PATH="${TESTDATA_DIR}/synthetic_store_recovery_expected.csv"
COMMIT_LOG_DIR="${TESTDATA_DIR}/commit_logs"
TASK_LOG_DIR="${TESTDATA_DIR}/task_logs"
REPLAYED_STATE_PATH="/tmp/data_agent_system_replayed_state.csv"
REPLAYED_EXPECTED_PATH="${TESTDATA_DIR}/replayed_state_expected.csv"
TASK_REPLAYED_STATE_PATH="/tmp/data_agent_system_task_replayed_state.csv"
TASK_REPLAYED_EXPECTED_PATH="${TESTDATA_DIR}/task_replayed_state_expected.csv"
TASK_RECOVERY_STATE_PATH="/tmp/data_agent_system_task_recovery_state.csv"
TASK_RECOVERY_EXPECTED_PATH="${TESTDATA_DIR}/task_recovery_expected.csv"
TASK_RUNTIME_RECOVERY_STATE_PATH="/tmp/data_agent_system_task_runtime_recovery_state.csv"
TASK_RUNTIME_RECOVERY_EXPECTED_PATH="${TESTDATA_DIR}/task_runtime_recovery_expected.csv"
TASK_RUNTIME_RECOVERY_DEMO_PATH="/tmp/data_agent_system_task_runtime_recovery_demo.txt"
TASK_RUNTIME_RECOVERY_DEMO_EXPECTED_PATH="${TESTDATA_DIR}/task_runtime_recovery_demo_expected.txt"
TASK_RUNTIME_RECOVERY_CONTINUE_DEMO_PATH="/tmp/data_agent_system_task_runtime_recovery_continue_demo.txt"
TASK_RUNTIME_RECOVERY_CONTINUE_DEMO_EXPECTED_PATH="${TESTDATA_DIR}/task_runtime_recovery_continue_demo_expected.txt"
STORE_CONFIG_PATH="${TESTDATA_DIR}/store_config_sample.txt"

python3 "${ROOT_DIR}/data_agent_system/experiments/parse_results.py" "${CSV_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_artifacts.py" "${CSV_PATH}" "${STORE_PATH}" "${SNAPSHOT_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_store_config.py" "${STORE_CONFIG_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_commit_logs.py" "${CSV_PATH}" "${COMMIT_LOG_DIR}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_task_event_logs.py" "${CSV_PATH}" "${TASK_LOG_DIR}"
python3 "${ROOT_DIR}/data_agent_system/experiments/commit_log_replay.py" "${COMMIT_LOG_DIR}" "${REPLAYED_STATE_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_replayed_state.py" "${REPLAYED_STATE_PATH}" "${REPLAYED_EXPECTED_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/task_event_log_replay.py" "${TASK_LOG_DIR}" "${TASK_REPLAYED_STATE_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_replayed_state.py" "${TASK_REPLAYED_STATE_PATH}" "${TASK_REPLAYED_EXPECTED_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/task_event_recovery.py" "${TASK_LOG_DIR}" "${TASK_RECOVERY_STATE_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_replayed_state.py" "${TASK_RECOVERY_STATE_PATH}" "${TASK_RECOVERY_EXPECTED_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/task_runtime_recovery.py" "${TASK_LOG_DIR}" "${TASK_RUNTIME_RECOVERY_STATE_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_replayed_state.py" "${TASK_RUNTIME_RECOVERY_STATE_PATH}" "${TASK_RUNTIME_RECOVERY_EXPECTED_PATH}"
LD_LIBRARY_PATH=/opt/gcc-11.4/lib64 "${ROOT_DIR}/build-gcc11/task_runtime_recovery_demo" "${TASK_LOG_DIR}" > "${TASK_RUNTIME_RECOVERY_DEMO_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_task_runtime_recovery_demo.py" "${TASK_RUNTIME_RECOVERY_DEMO_PATH}" "${TASK_RUNTIME_RECOVERY_DEMO_EXPECTED_PATH}"
LD_LIBRARY_PATH=/opt/gcc-11.4/lib64 "${ROOT_DIR}/build-gcc11/task_runtime_recovery_continue_demo" "${TASK_LOG_DIR}" > "${TASK_RUNTIME_RECOVERY_CONTINUE_DEMO_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_task_runtime_recovery_continue_demo.py" "${TASK_RUNTIME_RECOVERY_CONTINUE_DEMO_PATH}" "${TASK_RUNTIME_RECOVERY_CONTINUE_DEMO_EXPECTED_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_recovery_artifacts.py" \
  "${RECOVERY_STORE_PATH}" \
  "${RECOVERY_TEMP_PATH}" \
  "${RECOVERY_JOURNAL_PATH}" \
  "${RECOVERY_EXPECTED_PATH}"
