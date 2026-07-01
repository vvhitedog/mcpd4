#pragma once

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <decomp/partition_worker.h>
#include <mcpd4/protocol.h>
#include <mcpd4/tcp.h>

namespace mcpd4 {

constexpr std::uint32_t kProtocolVersion = 3;

struct RpcByteStats {
  std::uint64_t tx_bytes_total = 0;
  std::uint64_t rx_bytes_total = 0;
  std::uint64_t hello_tx_bytes = 0;
  std::uint64_t hello_rx_bytes = 0;
  std::uint64_t partition_load_tx_bytes = 0;
  std::uint64_t partition_load_rx_bytes = 0;
  std::uint64_t solve_request_tx_bytes = 0;
  std::uint64_t solve_request_rx_bytes = 0;
  std::uint64_t solve_result_tx_bytes = 0;
  std::uint64_t solve_result_rx_bytes = 0;
  std::uint64_t scale_objective_tx_bytes = 0;
  std::uint64_t scale_objective_rx_bytes = 0;
  std::uint64_t ready_tx_bytes = 0;
  std::uint64_t ready_rx_bytes = 0;
  std::uint64_t stop_tx_bytes = 0;
  std::uint64_t stop_rx_bytes = 0;
  std::uint64_t error_tx_bytes = 0;
  std::uint64_t error_rx_bytes = 0;
};

struct TcpPartitionWorkerTimingStats {
  std::uint64_t load_partition_rpc_wall_us = 0;
  std::uint64_t solve_round_rpc_wall_us = 0;
  std::uint64_t solve_round_worker_wall_us = 0;
  std::uint64_t scale_objective_rpc_wall_us = 0;
  long load_partition_rpc_count = 0;
  long partition_solve_call_count = 0;
  long solve_batch_rpc_count = 0;
  long scale_objective_rpc_count = 0;
  RpcByteStats rpc_bytes;
};

struct TcpPartitionWorkerStatusSnapshot {
  std::string worker_name;
  std::uint32_t cpu_count = 0;
  std::uint64_t ram_gb = 0;
  std::vector<int> partition_ids;
  TcpPartitionWorkerTimingStats timing;
};

class TcpPartitionWorker final : public mcpd3::PartitionWorker {
public:
  TcpPartitionWorker(SocketHandle socket, HelloMessage hello,
                     std::uint64_t hello_rx_bytes = 0);

  void loadPartition(const mcpd3::PartitionPackage &package) override;
  mcpd3::PartitionWorkerResourceEstimate resourceEstimate() const override;
  mcpd3::PartitionSolveResult solveRound(
      const mcpd3::PartitionSolveRequest &request) override;
  std::vector<mcpd3::PartitionSolveResult> solveRoundBatch(
      const std::vector<mcpd3::PartitionSolveRequest> &requests) override;
  void scaleObjective(long factor) override;

  void stop(std::uint32_t reason = 0, const std::string &message = "");
  const HelloMessage &hello() const { return hello_; }
  const TcpPartitionWorkerTimingStats &timingStats() const {
    return timing_stats_;
  }
  TcpPartitionWorkerStatusSnapshot statusSnapshot() const;

private:
  SocketHandle socket_;
  HelloMessage hello_;
  TcpPartitionWorkerTimingStats timing_stats_;
  std::vector<int> partition_ids_;
};

HelloMessage makeDefaultHello(const std::string &worker_name);

struct WorkerRuntimeStatusHooks {
  std::function<void(const std::string &phase)> on_phase;
  std::function<void(MessageType type, std::uint64_t bytes)> on_frame_sent;
  std::function<void(MessageType type, std::uint64_t bytes)> on_frame_received;
  std::function<void(int partition_id)> on_partition_loaded;
  std::function<void(long round_id, const std::vector<int> &partition_ids)>
      on_solve_start;
  std::function<void(std::uint64_t elapsed_us, long partition_solve_count,
                     bool batch)>
      on_solve_done;
  std::function<void(long factor)> on_scale_objective;
  std::function<void(const std::string &message)> on_error;
};

void runWorkerClient(const std::string &host, std::uint16_t port,
                     const HelloMessage &hello,
                     const WorkerRuntimeStatusHooks &status_hooks = {});

std::unique_ptr<TcpPartitionWorker> acceptTcpPartitionWorker(
    SocketHandle *listener, std::chrono::milliseconds timeout);

} // namespace mcpd4
