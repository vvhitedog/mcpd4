#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  cat >&2 <<'USAGE'
usage: scripts/run_local_process_benchmark.sh DIMACS [--directed]

Environment overrides:
  MCPD3_BUILD_DIR              build
  MCPD3_WORKERS                2
  MCPD3_PARTITIONS             2
  MCPD3_MAX_ITERATIONS         10000
  MCPD3_NUM_SCALES             5
  MCPD3_INITIAL_STEP           10000
  MCPD3_CAPACITY_MULTIPLIER    10000
  MCPD3_ACCEPT_TIMEOUT_MS      30000
  MCPD3_READY_TIMEOUT_SEC      300
  MCPD3_SATURATE_CAPACITY_OVERFLOW  0
USAGE
  exit 2
fi

dimacs_path=$1
shift

build_dir=${MCPD3_BUILD_DIR:-build}
workers=${MCPD3_WORKERS:-2}
partitions=${MCPD3_PARTITIONS:-2}
max_iterations=${MCPD3_MAX_ITERATIONS:-10000}
num_scales=${MCPD3_NUM_SCALES:-5}
initial_step=${MCPD3_INITIAL_STEP:-10000}
capacity_multiplier=${MCPD3_CAPACITY_MULTIPLIER:-10000}
accept_timeout_ms=${MCPD3_ACCEPT_TIMEOUT_MS:-30000}
ready_timeout_sec=${MCPD3_READY_TIMEOUT_SEC:-300}
saturate_capacity_overflow=${MCPD3_SATURATE_CAPACITY_OVERFLOW:-${MCPD3_TRUNCATE_CAPACITY_OVERFLOW:-0}}

coordinator="${build_dir}/mcpd3_coordinator"
worker="${build_dir}/mcpd3_worker"
if [[ ! -x "$coordinator" || ! -x "$worker" ]]; then
  echo "missing coordinator/worker binaries; run cmake --build ${build_dir} -j" >&2
  exit 2
fi

port=$(python3 - <<'PY'
import socket
with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
    s.bind(("127.0.0.1", 0))
    print(s.getsockname()[1])
PY
)
ready_file=$(mktemp /tmp/mcpd3-local-benchmark-ready.XXXXXX)
rm -f "$ready_file"

cleanup() {
  rm -f "$ready_file"
  if [[ -n "${coordinator_pid:-}" ]]; then
    kill "$coordinator_pid" 2>/dev/null || true
  fi
  if [[ -n "${worker_pids:-}" ]]; then
    for pid in $worker_pids; do
      kill "$pid" 2>/dev/null || true
    done
  fi
}
trap cleanup EXIT

extra_args=()
if [[ "$saturate_capacity_overflow" != "0" && -n "$saturate_capacity_overflow" ]]; then
  extra_args+=(--saturate-capacity-overflow)
fi

"$coordinator" "$dimacs_path" \
  --port "$port" \
  --workers "$workers" \
  --partitions "$partitions" \
  --max-iterations "$max_iterations" \
  --num-scales "$num_scales" \
  --initial-step "$initial_step" \
  --capacity-multiplier "$capacity_multiplier" \
  --accept-timeout-ms "$accept_timeout_ms" \
  --ready-file "$ready_file" \
  "${extra_args[@]}" \
  "$@" &
coordinator_pid=$!

ready_attempts=$((ready_timeout_sec * 100))
for _ in $(seq 1 "$ready_attempts"); do
  if [[ -s "$ready_file" ]]; then
    break
  fi
  if ! kill -0 "$coordinator_pid" 2>/dev/null; then
    wait "$coordinator_pid"
  fi
  sleep 0.01
done
if [[ ! -s "$ready_file" ]]; then
  echo "coordinator did not become ready" >&2
  exit 1
fi

worker_pids=
for idx in $(seq 1 "$workers"); do
  "$worker" 127.0.0.1 "$port" --name "local-benchmark-${idx}" &
  worker_pids="${worker_pids} $!"
done

set +e
wait "$coordinator_pid"
coordinator_status=$?
coordinator_pid=
for pid in $worker_pids; do
  wait "$pid"
  worker_status=$?
  if [[ "$worker_status" -ne 0 && "$coordinator_status" -eq 0 ]]; then
    coordinator_status=$worker_status
  fi
done
worker_pids=
set -e
exit "$coordinator_status"
