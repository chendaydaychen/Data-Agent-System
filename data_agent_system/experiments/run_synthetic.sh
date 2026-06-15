#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
BUILD_DIR="${ROOT_DIR}/build"
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
STORE_CONFIG_PATH="${OUTPUT_DIR}/store_config.txt"

mkdir -p "${OUTPUT_DIR}"
mkdir -p "${COMMIT_LOG_DIR}"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}" --target synthetic_experiment
"${BUILD_DIR}/synthetic_experiment" "${TASKS}" "${BRANCHES}" "${CONFLICT_EVERY}" "${CSV_PATH}" "${BACKEND}" "${STORE_PATH}" "${COMMIT_LOG_DIR}" "${NAMESPACE_PREFIX}" "${HOST}" "${PORT}" "${DATABASE_INDEX}" "${COLUMN_FAMILY}" "${STORE_CONFIG_PATH}"
"${ROOT_DIR}/data_agent_system/experiments/parse_results.py" "${CSV_PATH}"

echo "wrote ${CSV_PATH}"
