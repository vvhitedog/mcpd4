#pragma once

#include <decomp/partition_worker.h>

#include <memory>

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

} // namespace mcpd4
