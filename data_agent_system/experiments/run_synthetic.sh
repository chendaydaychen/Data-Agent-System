#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUTPUT_DIR="${1:-${ROOT_DIR}/output/synthetic}"
TASKS="${2:-12}"
BRANCHES="${3:-3}"
CONFLICT_EVERY="${4:-4}"
BACKEND="${5:-memory}"
STORE_PATH="${6:-${OUTPUT_DIR}/synthetic_store.tsv}"
CSV_PATH="${OUTPUT_DIR}/synthetic_results.csv"
COMMIT_LOG_DIR="${OUTPUT_DIR}/commit_logs"
NAMESPACE_PREFIX="${7:-synthetic}"
HOST="${8:-127.0.0.1}"
PORT="${9:-6379}"
DATABASE_INDEX="${10:-0}"
COLUMN_FAMILY="${11:-default}"
TASK_EVENT_LOG_DIR="${12:-${OUTPUT_DIR}/task_logs}"
STORE_CONFIG_PATH="${OUTPUT_DIR}/store_config.txt"

mkdir -p "${OUTPUT_DIR}"
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
  cmake --build --preset "${BUILD_PRESET}" --target synthetic_experiment
else
  BUILD_DIR="${DAS_BUILD_DIR:-${ROOT_DIR}/build-local-wsl}"
  cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}" -DCMAKE_BUILD_TYPE="${DAS_CMAKE_BUILD_TYPE:-Release}"
  cmake --build "${BUILD_DIR}" --target synthetic_experiment -j"${DAS_BUILD_JOBS:-2}"
fi

"${BUILD_DIR}/synthetic_experiment" "${TASKS}" "${BRANCHES}" "${CONFLICT_EVERY}" "${CSV_PATH}" "${BACKEND}" "${STORE_PATH}" "${COMMIT_LOG_DIR}" "${NAMESPACE_PREFIX}" "${HOST}" "${PORT}" "${DATABASE_INDEX}" "${COLUMN_FAMILY}" "${STORE_CONFIG_PATH}" "${TASK_EVENT_LOG_DIR}"
python3 "${ROOT_DIR}/data_agent_system/experiments/parse_results.py" "${CSV_PATH}"

echo "wrote ${CSV_PATH}"
echo "wrote ${COMMIT_LOG_DIR}"
echo "wrote ${TASK_EVENT_LOG_DIR}"
