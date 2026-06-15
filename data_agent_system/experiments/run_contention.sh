#!/usr/bin/env bash

set -euo pipefail

ROOT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
OUTPUT_DIR="${1:-${ROOT_DIR}/output/contention}"
TASKS="${2:-24}"
BRANCHES="${3:-4}"
CONFLICT_EVERY="${4:-2}"
BACKEND="${5:-memory}"
STORE_PATH="${6:-${OUTPUT_DIR}/contention_store.tsv}"
NAMESPACE_PREFIX="${7:-contention}"
HOST="${8:-127.0.0.1}"
PORT="${9:-6379}"
DATABASE_INDEX="${10:-0}"
COLUMN_FAMILY="${11:-default}"

"${ROOT_DIR}/data_agent_system/experiments/run_synthetic.sh" "${OUTPUT_DIR}" "${TASKS}" "${BRANCHES}" "${CONFLICT_EVERY}" "${BACKEND}" "${STORE_PATH}" "${NAMESPACE_PREFIX}" "${HOST}" "${PORT}" "${DATABASE_INDEX}" "${COLUMN_FAMILY}"
