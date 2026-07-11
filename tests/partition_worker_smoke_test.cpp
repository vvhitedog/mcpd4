#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>

#include <decomp/partition_worker.h>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void smokeTestInProcessPartitionWorkerFromSubmodule() {
  mcpd3::PartitionPackage package;
  package.partition_id = 0;
  package.local_node_count = 2;
  package.arcs = {0, 1};
  package.arc_capacities = {3, 5};
  package.terminal_capacities = {2, -4};
  package.local_to_global = {100, 200};
  package.constraint_endpoints.push_back(
      mcpd3::ConstraintEndpointBinding{/*constraint_id=*/7,
                                        /*global_node_id=*/200,
                                        /*local_index=*/1,
                                        /*is_source=*/true,
                                        /*alpha=*/0,
                                        /*last_alpha=*/0,
                                        /*alpha_momentum=*/0});

  mcpd3::InProcessPartitionWorker worker;
  worker.loadPartition(package);

  mcpd3::PartitionSolveRequest first_request;
  first_request.round_id = 1;
  first_request.scale = 10000;
  first_request.return_full_labels = true;
  auto first_result = worker.solveRound(first_request);
  require(first_result.round_id == 1, "first round id was not preserved");
  require(first_result.partition_id == 0, "partition id was not preserved");
  require(first_result.constrained_labels.size() == 1,
          "expected one constrained label");
  require(first_result.constrained_labels[0].constraint_id == 7,
          "constraint id was not preserved");
  require(first_result.constrained_labels[0].global_node_id == 200,
          "global node id was not preserved");
  require(first_result.constrained_labels[0].local_index == 1,
          "local index was not preserved");
  require(first_result.full_labels.size() == 2,
          "expected one full label per local node");
  require(first_result.full_labels[0].global_node_id == 100,
          "first full label global node id was not preserved");
  require(first_result.full_labels[0].local_index == 0,
          "first full label local index was not preserved");
  require(first_result.full_labels[1].global_node_id == 200,
          "second full label global node id was not preserved");
  require(first_result.full_labels[1].local_index == 1,
          "second full label local index was not preserved");

  mcpd3::PartitionSolveRequest second_request;
  second_request.round_id = 2;
  second_request.scale = 10000;
  second_request.alpha_updates.push_back(
      mcpd3::AlphaUpdate{/*constraint_id=*/7,
                          /*alpha=*/4,
                          /*last_alpha=*/0,
                          /*alpha_momentum=*/0});
  auto second_result = worker.solveRound(second_request);
  require(second_result.round_id == 2, "second round id was not preserved");
  require(second_result.constrained_labels.size() == 1,
          "expected one constrained label after alpha update");
}

mcpd3::PartitionPackage makeSingleBoundaryPackage() {
  mcpd3::PartitionPackage package;
  package.partition_id = 0;
  package.local_node_count = 1;
  package.terminal_capacities = {0};
  package.local_to_global = {10};
  package.constraint_endpoints.push_back(
      mcpd3::ConstraintEndpointBinding{/*constraint_id=*/3,
                                        /*global_node_id=*/10,
                                        /*local_index=*/0,
                                        /*is_source=*/true,
                                        /*alpha=*/0,
                                        /*last_alpha=*/0,
                                        /*alpha_momentum=*/0});
  return package;
}

void workerDerivesLastAlphaFromLocalState() {
  auto correct_package = makeSingleBoundaryPackage();
  auto bogus_package = makeSingleBoundaryPackage();

  mcpd3::InProcessPartitionWorker correct_worker;
  mcpd3::InProcessPartitionWorker bogus_worker;
  correct_worker.loadPartition(correct_package);
  bogus_worker.loadPartition(bogus_package);

  mcpd3::PartitionSolveRequest first_request;
  first_request.round_id = 1;
  first_request.partition_id = 0;
  (void)correct_worker.solveRound(first_request);
  (void)bogus_worker.solveRound(first_request);

  mcpd3::PartitionSolveRequest correct_update;
  correct_update.round_id = 2;
  correct_update.partition_id = 0;
  correct_update.alpha_updates.push_back(
      mcpd3::AlphaUpdate{/*constraint_id=*/3,
                          /*alpha=*/7,
                          /*last_alpha=*/0,
                          /*alpha_momentum=*/0});

  mcpd3::PartitionSolveRequest bogus_update = correct_update;
  bogus_update.alpha_updates[0].last_alpha = 123456;
  bogus_update.alpha_updates[0].alpha_momentum = -99.0f;

  const auto correct_result = correct_worker.solveRound(correct_update);
  const auto bogus_result = bogus_worker.solveRound(bogus_update);

  require(bogus_result.lower_bound == correct_result.lower_bound,
          "worker should derive last alpha locally");
  require(bogus_result.constrained_labels.size() ==
              correct_result.constrained_labels.size(),
          "bogus alpha metadata should not change boundary label count");
  require(bogus_result.constrained_labels[0].label ==
              correct_result.constrained_labels[0].label,
          "bogus alpha metadata should not change boundary label");
}

} // namespace

int main() {
  try {
    smokeTestInProcessPartitionWorkerFromSubmodule();
    workerDerivesLastAlphaFromLocalState();
  } catch (const std::exception &e) {
    std::cerr << "partition_worker_smoke_test failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
