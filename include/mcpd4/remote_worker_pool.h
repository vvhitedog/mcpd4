#pragma once

#include <mcpd4/runtime.h>
#include <mcpd4/shared_partition_worker.h>

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace mcpd4 {

struct RemoteWorkerPoolListeningInfo {
  std::uint16_t tcp_port = 0;
  std::uint16_t discovery_port = 0;
  int min_worker_count = 0;
};

struct RemoteWorkerPoolOptions {
  std::string bind_host = "0.0.0.0";
  std::string advertise_host;
  std::uint16_t tcp_port = 0;
  bool enable_discovery = false;
  std::uint16_t discovery_port = 0;
  std::string discovery_token = "mcpd4";
  int min_worker_count = 1;
  std::chrono::milliseconds accept_timeout = std::chrono::minutes(10);
  TransportCompression compression = TransportCompression::NONE;
  std::function<void(const RemoteWorkerPoolListeningInfo &)> on_listening;
  std::function<void(const TcpPartitionWorkerStatusSnapshot &)> on_accepted;
};

class RemoteWorkerPool {
public:
  static std::shared_ptr<RemoteWorkerPool>
  accept(const RemoteWorkerPoolOptions &options);

  ~RemoteWorkerPool();

  std::shared_ptr<SharedPartitionWorkerPool> sharedPool() const {
    return shared_pool_;
  }
  std::size_t workerCount() const { return remote_workers_.size(); }
  std::vector<TcpPartitionWorkerStatusSnapshot> statusSnapshots() const;
  void stop(std::uint32_t reason = 0, const std::string &message = "");

private:
  RemoteWorkerPool() = default;

  std::shared_ptr<SharedPartitionWorkerPool> shared_pool_ =
      std::make_shared<SharedPartitionWorkerPool>();
  std::vector<TcpPartitionWorker *> remote_workers_;
  bool stopped_ = false;
};

} // namespace mcpd4
