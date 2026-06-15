#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
TIMESTAMP=$(date +%Y%m%d-%H%M%S)
OUTPUT_DIR="${1:-${ROOT_DIR}/output/e2e/${TIMESTAMP}}"
TASKS="${2:-6}"
BRANCHES="${3:-3}"
CONFLICT_EVERY="${4:-3}"
BACKEND="${5:-file}"
NAMESPACE_PREFIX="${6:-e2e}"
HOST="${7:-127.0.0.1}"
PORT="${8:-6379}"
DATABASE_INDEX="${9:-0}"
COLUMN_FAMILY="${10:-default}"

if [[ -e "${OUTPUT_DIR}" ]] &&
   [[ -n "$(find "${OUTPUT_DIR}" -mindepth 1 -maxdepth 1 -print -quit)" ]] &&
   [[ "${DAS_E2E_ALLOW_EXISTING:-0}" != "1" ]]; then
  echo "output directory is not empty: ${OUTPUT_DIR}" >&2
  echo "set DAS_E2E_ALLOW_EXISTING=1 to reuse it" >&2
  exit 1
fi

RETRY_OUTPUT_PATH="${OUTPUT_DIR}/retry_loop.txt"
RETRY_TASK_LOG_PATH="${OUTPUT_DIR}/retry_loop.task.log"
SYNTHETIC_DIR="${OUTPUT_DIR}/synthetic"
CSV_PATH="${SYNTHETIC_DIR}/synthetic_results.csv"
STORE_PATH="${SYNTHETIC_DIR}/synthetic_store.tsv"
COMMIT_LOG_DIR="${SYNTHETIC_DIR}/commit_logs"
TASK_EVENT_LOG_DIR="${SYNTHETIC_DIR}/task_logs"
STORE_CONFIG_PATH="${SYNTHETIC_DIR}/store_config.txt"
SYNTHETIC_SUMMARY_PATH="${SYNTHETIC_DIR}/synthetic_summary.csv"
TASK_RECOVERY_PATH="${SYNTHETIC_DIR}/task_recovery.csv"
RUNTIME_RECOVERY_PATH="${SYNTHETIC_DIR}/task_runtime_recovery.csv"
SUMMARY_PATH="${OUTPUT_DIR}/summary.txt"

mkdir -p "${OUTPUT_DIR}"
mkdir -p "${SYNTHETIC_DIR}"
mkdir -p "${COMMIT_LOG_DIR}"
mkdir -p "${TASK_EVENT_LOG_DIR}"

CONFIGURE_PRESET="${DAS_CMAKE_CONFIGURE_PRESET:-}"
BUILD_PRESET="${DAS_CMAKE_BUILD_PRESET:-}"
if [[ -z "${CONFIGURE_PRESET}" && -x /opt/gcc-11.4/bin/g++ ]]; then
  CONFIGURE_PRESET="gcc11-release"
fi

if [[ -n "${CONFIGURE_PRESET}" ]]; then
  case "${CONFIGURE_PRESET}" in
    gcc11-release)
      BUILD_DIR="${DAS_BUILD_DIR:-${ROOT_DIR}/build-gcc11}"
      BUILD_PRESET="${BUILD_PRESET:-build-gcc11-release}"
      ;;
    gcc15-release)
      BUILD_DIR="${DAS_BUILD_DIR:-${ROOT_DIR}/build-gcc15}"
      BUILD_PRESET="${BUILD_PRESET:-build-gcc15-release}"
      ;;
    *)
      if [[ -z "${DAS_BUILD_DIR:-}" ]]; then
        echo "unknown configure preset '${CONFIGURE_PRESET}'; set DAS_BUILD_DIR to the preset binary dir" >&2
        exit 1
      fi
      BUILD_DIR="${DAS_BUILD_DIR}"
      ;;
  esac
  cmake --preset "${CONFIGURE_PRESET}"
  cmake --build --preset "${BUILD_PRESET}" --target agent_retry_loop_demo synthetic_experiment
else
  BUILD_DIR="${DAS_BUILD_DIR:-${ROOT_DIR}/build-local-wsl}"
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${DAS_CMAKE_BUILD_TYPE:-Release}"
  cmake --build "${BUILD_DIR}" --target agent_retry_loop_demo synthetic_experiment -j"${DAS_BUILD_JOBS:-2}"
fi

"${BUILD_DIR}/agent_retry_loop_demo" > "${RETRY_OUTPUT_PATH}"
cp /tmp/data_agent_system_retry_loop.task.log "${RETRY_TASK_LOG_PATH}"

grep -qx "first_commit=0" "${RETRY_OUTPUT_PATH}"
grep -qx "recovery_action=RETRY_FROM_SCRATCH" "${RETRY_OUTPUT_PATH}"
grep -qx "recovery_command=SUBMIT_FRESH_TASK" "${RETRY_OUTPUT_PATH}"
grep -qx "retry_committed=1" "${RETRY_OUTPUT_PATH}"
grep -qx "output_after_retry=recovered_agent_result" "${RETRY_OUTPUT_PATH}"

"${BUILD_DIR}/synthetic_experiment" \
  "${TASKS}" \
  "${BRANCHES}" \
  "${CONFLICT_EVERY}" \
  "${CSV_PATH}" \
  "${BACKEND}" \
  "${STORE_PATH}" \
  "${COMMIT_LOG_DIR}" \
  "${NAMESPACE_PREFIX}" \
  "${HOST}" \
  "${PORT}" \
  "${DATABASE_INDEX}" \
  "${COLUMN_FAMILY}" \
  "${STORE_CONFIG_PATH}" \
  "${TASK_EVENT_LOG_DIR}"

python3 "${ROOT_DIR}/data_agent_system/experiments/parse_results.py" "${CSV_PATH}" > "${SYNTHETIC_SUMMARY_PATH}"
if [[ "${BACKEND}" == "memory" ]]; then
  python3 "${ROOT_DIR}/data_agent_system/experiments/verify_artifacts.py" "${CSV_PATH}"
else
  python3 "${ROOT_DIR}/data_agent_system/experiments/verify_artifacts.py" "${CSV_PATH}" "${STORE_PATH}"
fi
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_store_config.py" "${STORE_CONFIG_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_commit_logs.py" "${CSV_PATH}" "${COMMIT_LOG_DIR}"
python3 "${ROOT_DIR}/data_agent_system/experiments/verify_task_event_logs.py" "${CSV_PATH}" "${TASK_EVENT_LOG_DIR}"
python3 "${ROOT_DIR}/data_agent_system/experiments/task_event_recovery.py" "${TASK_EVENT_LOG_DIR}" "${TASK_RECOVERY_PATH}"
python3 "${ROOT_DIR}/data_agent_system/experiments/task_runtime_recovery.py" "${TASK_EVENT_LOG_DIR}" "${RUNTIME_RECOVERY_PATH}"

python3 - "${CSV_PATH}" "${RUNTIME_RECOVERY_PATH}" <<'PY'
import csv
import sys

csv_path, recovery_path = sys.argv[1], sys.argv[2]
with open(csv_path, newline="") as handle:
    rows = list(csv.DictReader(handle))
if not rows:
    raise SystemExit("synthetic result must contain rows")
committed = sum(int(row["committed"]) for row in rows)
aborted = len(rows) - committed
if committed < 1:
    raise SystemExit("closed loop requires at least one committed synthetic task")
if aborted < 1:
    raise SystemExit("closed loop requires at least one aborted synthetic task")

with open(recovery_path, newline="") as handle:
    recovery_rows = list(csv.DictReader(handle))
actions = {row["recovery_action"] for row in recovery_rows}
commands = {row["recovery_command"] for row in recovery_rows}
if "SKIP_COMMITTED" not in actions:
    raise SystemExit("recovery output must include SKIP_COMMITTED")
if "RETRY_FROM_SCRATCH" not in actions:
    raise SystemExit("recovery output must include RETRY_FROM_SCRATCH")
if "NOOP" not in commands:
    raise SystemExit("runtime recovery output must include NOOP")
if "SUBMIT_FRESH_TASK" not in commands:
    raise SystemExit("runtime recovery output must include SUBMIT_FRESH_TASK")
print("closed_loop_recovery_verification=ok")
PY

{
  echo "output_dir=${OUTPUT_DIR}"
  echo "build_dir=${BUILD_DIR}"
  echo "tasks=${TASKS}"
  echo "branches=${BRANCHES}"
  echo "conflict_every=${CONFLICT_EVERY}"
  echo "backend=${BACKEND}"
  echo
  echo "[retry_loop]"
  cat "${RETRY_OUTPUT_PATH}"
  echo
  echo "[synthetic_summary]"
  cat "${SYNTHETIC_SUMMARY_PATH}"
  echo
  echo "[artifacts]"
  echo "results=${CSV_PATH}"
  echo "store=${STORE_PATH}"
  echo "commit_logs=${COMMIT_LOG_DIR}"
  echo "task_event_logs=${TASK_EVENT_LOG_DIR}"
  echo "task_recovery=${TASK_RECOVERY_PATH}"
  echo "runtime_recovery=${RUNTIME_RECOVERY_PATH}"
  echo
  echo "verification=ok"
} > "${SUMMARY_PATH}"

cat "${SUMMARY_PATH}"
