#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${ROOT_DIR}/build"
PORT="${PORT:-45678}"
SNAPSHOT="${TMPDIR:-/tmp}/kv_store_integration_${PORT}.db"

cmake -S "${ROOT_DIR}" -B "${BUILD_DIR}"
cmake --build "${BUILD_DIR}"

rm -f "${SNAPSHOT}" "${SNAPSHOT}.wal" "${SNAPSHOT}.tmp"

"${BUILD_DIR}/kv_store" "${PORT}" "${SNAPSHOT}" 2 >"${SNAPSHOT}.server.log" 2>&1 &
SERVER_PID=$!

cleanup() {
  kill "${SERVER_PID}" >/dev/null 2>&1 || true
  wait "${SERVER_PID}" >/dev/null 2>&1 || true
  rm -f "${SNAPSHOT}" "${SNAPSHOT}.wal" "${SNAPSHOT}.tmp" "${SNAPSHOT}.server.log"
}
trap cleanup EXIT

for _ in {1..50}; do
  if nc -z 127.0.0.1 "${PORT}" >/dev/null 2>&1; then
    break
  fi
  sleep 0.1
done

OUTPUT="$(
  printf 'PING\nSET name Moat\nGET name\nSET city NYC\nSET lang C++\nGET name\nGET city\nGET lang\nSTATS\nQUIT\n' |
    nc 127.0.0.1 "${PORT}"
)"

echo "${OUTPUT}" | grep -q 'PONG'
echo "${OUTPUT}" | grep -q 'Moat'
echo "${OUTPUT}" | grep -q '(nil)'
echo "${OUTPUT}" | grep -q 'C++'
echo "${OUTPUT}" | grep -q 'keys=2'
echo "Integration test passed"
