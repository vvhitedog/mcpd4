#include <mcpd4/shared_partition_worker.h>

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Fn>
void requireThrowsContaining(Fn fn, const std::string &needle,
                             const std::string &message) {
  try {
    fn();
  } catch (const std::exception &error) {
    if (std::string(error.what()).find(needle) != std::string::npos) {
      return;
    }
    throw std::runtime_error(message + ": " + error.what());
  }
  throw std::runtime_error(message + ": no exception");
}

mcpd3::PartitionPackage makePackage(mcpd3::Capacity edge_capacity,
                                    int first_global_node_id) {
  mcpd3::PartitionPackage package;
  package.partition_id = 0;
  package.local_node_count = 2;
  package.arcs = {0, 1};
  package.arc_capacities = {edge_capacity, 0};
  package.terminal_capacities = {9, -9};
  package.local_to_global = {first_global_node_id,
                             first_global_node_id + 100};
  return package;
}

mcpd3::PartitionSolveResult solve(mcpd3::PartitionWorker *worker,
                                  long round_id) {
  mcpd3::PartitionSolveRequest request;
  request.round_id = round_id;
  request.partition_id = 0;
  return worker->solveRound(request);
}

struct RecordingWorkerState {
  std::vector<std::vector<int>> unloads;
};

class RecordingPartitionWorker final : public mcpd3::PartitionWorker {
public:
  explicit RecordingPartitionWorker(
      std::shared_ptr<RecordingWorkerState> state)
      : state_(std::move(state)) {}

  void loadPartition(const mcpd3::PartitionPackage &package) override {
    worker_.loadPartition(package);
  }
  void unloadPartitions(const std::vector<int> &partition_ids) override {
    worker_.unloadPartitions(partition_ids);
    state_->unloads.push_back(partition_ids);
  }
  mcpd3::PartitionSolveResult solveRound(
      const mcpd3::PartitionSolveRequest &request) override {
    return worker_.solveRound(request);
  }
  void scaleObjective(long factor,
                      bool saturate_capacity_overflow = false) override {
    worker_.scaleObjective(factor, saturate_capacity_overflow);
  }

private:
  std::shared_ptr<RecordingWorkerState> state_;
  mcpd3::InProcessPartitionWorker worker_;
};

void namespacesIsolateIdenticalLocalPartitionIds() {
  auto physical = std::make_unique<mcpd3::InProcessPartitionWorker>();
  auto connection =
      std::make_shared<mcpd4::SharedPartitionWorkerConnection>(
          std::move(physical));
  auto first = connection->makeNamespace();
  auto second = connection->makeNamespace();

  first->loadPartition(makePackage(/*edge_capacity=*/3,
                                   /*first_global_node_id=*/9));
  second->loadPartition(makePackage(/*edge_capacity=*/5,
                                    /*first_global_node_id=*/10));

  const auto first_before = solve(first.get(), 1);
  const auto second_before = solve(second.get(), 1);
  require(first_before.partition_id == 0 && second_before.partition_id == 0,
          "each namespace must expose its original local partition id");
  require(first_before.lower_bound != second_before.lower_bound,
          "test namespaces need distinguishable objectives");

  first->scaleObjective(/*factor=*/2);
  const auto first_scaled = solve(first.get(), 2);
  const auto second_unscaled = solve(second.get(), 2);
  require(first_scaled.lower_bound == first_before.lower_bound * 2,
          "namespace-global scaling must affect its owned partition");
  require(second_unscaled.lower_bound == second_before.lower_bound,
          "namespace-global scaling must not affect another namespace");

  mcpd3::PartitionCapacityUpdate update;
  update.partition_id = 0;
  update.arc_capacities = {12, 0};
  update.terminal_capacities = {36, -36};
  update.preserve_flow_state = true;
  update.flow_scale_numerator = 2;
  update.flow_scale_denominator = 1;
  first->replacePartitionCapacities(update);
  const auto first_updated = solve(first.get(), 3);
  const auto second_still_unscaled = solve(second.get(), 3);
  require(first_updated.lower_bound != first_scaled.lower_bound,
          "capacity refresh must target the namespace-owned partition");
  require(second_still_unscaled.lower_bound == second_before.lower_bound,
          "capacity refresh must not alter another namespace");

  std::vector<mcpd3::NodeLabel> first_labels(2);
  std::vector<mcpd3::NodeLabel> second_labels(2);
  first->copyFullLabels(0, 0, first_labels.data(), first_labels.size());
  second->copyFullLabels(0, 0, second_labels.data(), second_labels.size());
  require(first_labels.front().global_node_id == 9 &&
              second_labels.front().global_node_id == 10,
          "bounded labels must be read from the correct namespace");
}

void namespaceRejectsInvalidLocalOperations() {
  auto connection =
      std::make_shared<mcpd4::SharedPartitionWorkerConnection>(
          std::make_unique<mcpd3::InProcessPartitionWorker>());
  auto worker = connection->makeNamespace();
  worker->loadPartition(makePackage(/*edge_capacity=*/3,
                                    /*first_global_node_id=*/9));

  requireThrowsContaining(
      [&] { worker->loadPartition(makePackage(4, 9)); },
      "already loaded",
      "namespace must reject duplicate local partition IDs");
  requireThrowsContaining(
      [&] {
        mcpd3::PartitionSolveRequest request;
        request.partition_id = 99;
        (void)worker->solveRound(request);
      },
      "unknown namespace partition id",
      "namespace must reject unknown solve IDs");
  requireThrowsContaining(
      [&] { worker->scaleObjectivePartitions({}, 2); },
      "at least one partition id",
      "namespace must reject an empty scoped scale");
  requireThrowsContaining(
      [&] { worker->scaleObjectivePartitions({0, 0}, 2); },
      "duplicate objective scale partition id",
      "namespace must reject duplicate scoped scale IDs");
  requireThrowsContaining(
      [&] { worker->scaleObjectivePartitions({99}, 2); },
      "unknown namespace partition id",
      "namespace must reject unknown scoped scale IDs");

  const auto resources = worker->resourceEstimate();
  require(resources.cpu_count == 1,
          "namespace must forward the physical worker resource estimate");
}

void namespaceExplicitlyAndAutomaticallyReleasesRemotePartitions() {
  auto state = std::make_shared<RecordingWorkerState>();
  auto connection =
      std::make_shared<mcpd4::SharedPartitionWorkerConnection>(
          std::make_unique<RecordingPartitionWorker>(state));
  {
    auto worker = connection->makeNamespace();
    worker->loadPartition(makePackage(3, 9));
    auto second_package = makePackage(5, 10);
    second_package.partition_id = 1;
    worker->loadPartition(second_package);

    requireThrowsContaining(
        [&] { worker->unloadPartitions({}); }, "at least one partition id",
        "namespace must reject an empty unload selection");
    requireThrowsContaining(
        [&] { worker->unloadPartitions({0, 0}); },
        "duplicate partition unload id",
        "namespace must reject duplicate unload partition IDs");
    requireThrowsContaining(
        [&] { worker->unloadPartitions({0, 99}); },
        "unknown namespace partition id",
        "namespace must validate every unload ID before releasing any");
    require(state->unloads.empty(),
            "invalid namespace unload must not reach the physical worker");

    worker->unloadPartitions({0});
    require(state->unloads == std::vector<std::vector<int>>{{0}},
            "explicit namespace unload must translate the remote ID");
    requireThrowsContaining(
        [&] { (void)solve(worker.get(), 1); },
        "unknown namespace partition id",
        "explicitly unloaded local ID must become unknown");
    worker->loadPartition(makePackage(7, 11));
    require(solve(worker.get(), 2).lower_bound != 0,
            "an explicitly released local ID must be reusable");
  }

  require(state->unloads.size() == 2,
          "namespace destruction must release all remaining partitions");
  auto destructor_ids = state->unloads.back();
  std::sort(destructor_ids.begin(), destructor_ids.end());
  require(destructor_ids == std::vector<int>({1, 2}),
          "namespace destruction must release exactly its remote IDs");
}

void poolCreatesOneNamespacePerPhysicalWorker() {
  mcpd4::SharedPartitionWorkerPool pool;
  requireThrowsContaining(
      [&] { (void)pool.makeNamespaceWorkers(); }, "pool is empty",
      "empty shared worker pool must reject workspace creation");
  pool.addWorker(std::make_unique<mcpd3::InProcessPartitionWorker>());
  pool.addWorker(std::make_unique<mcpd3::InProcessPartitionWorker>());
  require(pool.workerCount() == 2,
          "shared worker pool should count physical workers");
  auto first_workspace = pool.makeNamespaceWorkers();
  auto second_workspace = pool.makeNamespaceWorkers();
  require(first_workspace.size() == 2 && second_workspace.size() == 2,
          "each workspace should receive one proxy per physical worker");
}

} // namespace

int main() {
  try {
    namespacesIsolateIdenticalLocalPartitionIds();
    namespaceRejectsInvalidLocalOperations();
    namespaceExplicitlyAndAutomaticallyReleasesRemotePartitions();
    poolCreatesOneNamespacePerPhysicalWorker();
  } catch (const std::exception &error) {
    std::cerr << "shared_partition_worker_test failed: " << error.what()
              << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
