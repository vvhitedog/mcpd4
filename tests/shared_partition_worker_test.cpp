#include <mcpd4/shared_partition_worker.h>

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

} // namespace

int main() {
  try {
    namespacesIsolateIdenticalLocalPartitionIds();
    namespaceRejectsInvalidLocalOperations();
  } catch (const std::exception &error) {
    std::cerr << "shared_partition_worker_test failed: " << error.what()
              << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
