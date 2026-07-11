#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
  cat >&2 <<'USAGE'
usage: scripts/run_local_process_benchmark.sh DIMACS [--directed]

Environment overrides:
  MCPD4_BUILD_DIR              build
  MCPD4_WORKERS                2
  MCPD4_PARTITIONS             2
  MCPD4_MAX_ITERATIONS         10000
  MCPD4_SCHEDULE_LEVELS        5
  MCPD4_SCHEDULE_START         10000
  MCPD4_OBJECTIVE_SCALE        10000
  MCPD4_ACCEPT_TIMEOUT_MS      30000
  MCPD4_READY_TIMEOUT_SEC      300
  MCPD4_PROGRESS_EVERY         0
  MCPD4_TELEMETRY_CSV_PREFIX
  MCPD4_RPC_COMPRESSION        none
  MCPD4_WORKER_BK_STORAGE      worker default is file_mmap
  MCPD4_WORKER_BK_MMAP_DIR_PREFIX
  MCPD4_WORKER_BK_MMAP_ADVISE
  MCPD4_SATURATE_CAPACITY_OVERFLOW  0

Legacy MCPD4_NUM_SCALES, MCPD4_INITIAL_STEP, MCPD4_CAPACITY_MULTIPLIER, and
MCPD3_* aliases are still accepted for compatibility.
USAGE
  exit 2
fi

dimacs_path=$1
shift

build_dir=${MCPD4_BUILD_DIR:-${MCPD3_BUILD_DIR:-build}}
workers=${MCPD4_WORKERS:-${MCPD3_WORKERS:-2}}
partitions=${MCPD4_PARTITIONS:-${MCPD3_PARTITIONS:-2}}
max_iterations=${MCPD4_MAX_ITERATIONS:-${MCPD3_MAX_ITERATIONS:-10000}}
schedule_levels=${MCPD4_SCHEDULE_LEVELS:-${MCPD4_NUM_SCALES:-${MCPD3_NUM_SCALES:-5}}}
schedule_start=${MCPD4_SCHEDULE_START:-${MCPD4_INITIAL_STEP:-${MCPD3_INITIAL_STEP:-10000}}}
objective_scale=${MCPD4_OBJECTIVE_SCALE:-${MCPD4_CAPACITY_MULTIPLIER:-${MCPD3_CAPACITY_MULTIPLIER:-10000}}}
accept_timeout_ms=${MCPD4_ACCEPT_TIMEOUT_MS:-${MCPD3_ACCEPT_TIMEOUT_MS:-30000}}
ready_timeout_sec=${MCPD4_READY_TIMEOUT_SEC:-${MCPD3_READY_TIMEOUT_SEC:-300}}
progress_every=${MCPD4_PROGRESS_EVERY:-${MCPD3_PROGRESS_EVERY:-0}}
telemetry_csv_prefix=${MCPD4_TELEMETRY_CSV_PREFIX:-}
rpc_compression=${MCPD4_RPC_COMPRESSION:-none}
worker_bk_storage=${MCPD4_WORKER_BK_STORAGE:-}
worker_bk_mmap_dir_prefix=${MCPD4_WORKER_BK_MMAP_DIR_PREFIX:-}
worker_bk_mmap_advise=${MCPD4_WORKER_BK_MMAP_ADVISE:-}
saturate_capacity_overflow=${MCPD4_SATURATE_CAPACITY_OVERFLOW:-${MCPD3_SATURATE_CAPACITY_OVERFLOW:-${MCPD3_TRUNCATE_CAPACITY_OVERFLOW:-0}}}

coordinator="${build_dir}/mcpd4_coordinator"
worker="${build_dir}/mcpd4_worker"
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
ready_file=$(mktemp /tmp/mcpd4-local-benchmark-ready.XXXXXX)
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
if [[ -n "$telemetry_csv_prefix" ]]; then
  extra_args+=(--telemetry-csv-prefix "$telemetry_csv_prefix")
fi

"$coordinator" "$dimacs_path" \
  --port "$port" \
  --workers "$workers" \
  --partitions "$partitions" \
  --max-iterations "$max_iterations" \
  --schedule-levels "$schedule_levels" \
  --schedule-start "$schedule_start" \
  --objective-scale "$objective_scale" \
  --accept-timeout-ms "$accept_timeout_ms" \
  --progress-every "$progress_every" \
  --rpc-compression "$rpc_compression" \
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
  worker_args=(--name "local-benchmark-${idx}" --rpc-compression "$rpc_compression")
  if [[ -n "$worker_bk_storage" ]]; then
    worker_args+=(--bk-storage "$worker_bk_storage")
  fi
  if [[ -n "$worker_bk_mmap_dir_prefix" ]]; then
    worker_bk_mmap_dir="${worker_bk_mmap_dir_prefix}-${idx}"
    mkdir -p "$worker_bk_mmap_dir"
    worker_args+=(--bk-mmap-dir "$worker_bk_mmap_dir")
  fi
  if [[ -n "$worker_bk_mmap_advise" ]]; then
    worker_args+=(--bk-mmap-advise "$worker_bk_mmap_advise")
  fi
  "$worker" 127.0.0.1 "$port" "${worker_args[@]}" &
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
