#pragma once

#include <decomp/partition_worker.h>

#include <memory>
#include <vector>

namespace mcpd4 {

// Serializes one physical worker connection and creates isolated local-ID
// views suitable for independent persistent coordinator workspaces.
class SharedPartitionWorkerConnection {
public:
  struct State;

  explicit SharedPartitionWorkerConnection(
      std::unique_ptr<mcpd3::PartitionWorker> worker);

  std::unique_ptr<mcpd3::PartitionWorker> makeNamespace();

private:
  std::shared_ptr<State> state_;
};

class SharedPartitionWorkerPool {
public:
  void addWorker(std::unique_ptr<mcpd3::PartitionWorker> worker);
  std::size_t workerCount() const { return connections_.size(); }
  std::vector<std::unique_ptr<mcpd3::PartitionWorker>>
  makeNamespaceWorkers() const;

private:
  std::vector<std::shared_ptr<SharedPartitionWorkerConnection>> connections_;
};

} // namespace mcpd4
