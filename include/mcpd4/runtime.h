#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <string>

#include <decomp/partition_worker.h>
#include <mcpd4/protocol.h>
#include <mcpd4/tcp.h>

namespace mcpd4 {

constexpr std::uint32_t kProtocolVersion = 2;

struct TcpPartitionWorkerTimingStats {
  std::uint64_t load_partition_rpc_wall_us = 0;
  std::uint64_t solve_round_rpc_wall_us = 0;
  std::uint64_t solve_round_worker_wall_us = 0;
  std::uint64_t scale_objective_rpc_wall_us = 0;
  long load_partition_rpc_count = 0;
  long partition_solve_call_count = 0;
  long solve_batch_rpc_count = 0;
  long scale_objective_rpc_count = 0;
};

class TcpPartitionWorker final : public mcpd3::PartitionWorker {
public:
  TcpPartitionWorker(SocketHandle socket, HelloMessage hello);

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

private:
  SocketHandle socket_;
  HelloMessage hello_;
  TcpPartitionWorkerTimingStats timing_stats_;
};

HelloMessage makeDefaultHello(const std::string &worker_name);
void runWorkerClient(const std::string &host, std::uint16_t port,
                     const HelloMessage &hello);

std::unique_ptr<TcpPartitionWorker> acceptTcpPartitionWorker(
    SocketHandle *listener, std::chrono::milliseconds timeout);

} // namespace mcpd4
