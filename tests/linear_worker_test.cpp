#include <mcpd4/linear_worker.h>

#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

void requireNear(double actual, double expected, double tolerance,
                 const std::string &message) {
  if (std::abs(actual - expected) > tolerance) {
    throw std::runtime_error(message + ": expected " +
                             std::to_string(expected) + ", got " +
                             std::to_string(actual));
  }
}

template <typename Fn> void requireThrows(Fn fn, const std::string &message) {
  try {
    fn();
  } catch (const std::exception &) {
    return;
  }
  throw std::runtime_error(message);
}

mcpd4::LinearSystemPartition makeLeftPartition() {
  mcpd4::LinearSystemPartition partition;
  partition.partition_id = 0;
  partition.owned_global_nodes = {0, 1};
  partition.ghost_global_nodes = {2};
  partition.row_offsets = {0, 2, 5};
  partition.column_indices = {0, 1, 0, 1, 2};
  partition.values = {4.0, -1.0, -1.0, 4.0, -1.0};
  partition.rhs = {15.0, 10.0};
  partition.initial_x = {0.0, 0.0};
  partition.boundary_owned_local_indices = {1};
  return partition;
}

mcpd4::LinearSystemPartition makeRightPartition() {
  mcpd4::LinearSystemPartition partition;
  partition.partition_id = 1;
  partition.owned_global_nodes = {2};
  partition.ghost_global_nodes = {1};
  partition.row_offsets = {0, 2};
  partition.column_indices = {0, 1};
  partition.values = {3.0, -1.0};
  partition.rhs = {10.0};
  partition.initial_x = {0.0};
  partition.boundary_owned_local_indices = {0};
  return partition;
}

void splitPcgRecoversKnownSolution() {
  mcpd4::ResidentLinearPartition left(makeLeftPartition());
  mcpd4::ResidentLinearPartition right(makeRightPartition());

  auto left_initial = left.initialize({0.0});
  auto right_initial = right.initialize({0.0});
  const double rhs_norm_squared =
      left_initial.rhs_norm_squared + right_initial.rhs_norm_squared;
  double residual_inner = left_initial.residual_preconditioned_inner +
                          right_initial.residual_preconditioned_inner;
  double relative_residual = std::sqrt(
      (left_initial.residual_norm_squared +
       right_initial.residual_norm_squared) /
      rhs_norm_squared);
  require(relative_residual > 1e-12,
          "zero initialization should not solve the split system");

  std::vector<double> left_boundary = left_initial.boundary_direction;
  std::vector<double> right_boundary = right_initial.boundary_direction;
  int iterations = 0;
  while (relative_residual > 1e-12 && iterations < 10) {
    const auto left_product = left.multiply({right_boundary.at(0)});
    const auto right_product = right.multiply({left_boundary.at(0)});
    const double denominator = left_product.direction_product_inner +
                               right_product.direction_product_inner;
    const double alpha = residual_inner / denominator;
    const auto left_update = left.updateAlpha(alpha);
    const auto right_update = right.updateAlpha(alpha);
    relative_residual = std::sqrt(
        (left_update.residual_norm_squared +
         right_update.residual_norm_squared) /
        rhs_norm_squared);
    ++iterations;
    if (relative_residual <= 1e-12) {
      break;
    }
    const double next_inner =
        left_update.residual_preconditioned_inner +
        right_update.residual_preconditioned_inner;
    const double beta = next_inner / residual_inner;
    left_boundary = left.updateBeta(beta).boundary_direction;
    right_boundary = right.updateBeta(beta).boundary_direction;
    residual_inner = next_inner;
  }

  require(iterations <= 3, "three-variable PCG should converge in at most n steps");
  require(relative_residual <= 1e-12,
          "split resident PCG should reach the requested residual");
  for (double value : left.solution()) {
    requireNear(value, 5.0, 1e-10,
                "left partition should recover the known solution");
  }
  requireNear(right.solution().at(0), 5.0, 1e-10,
              "right partition should recover the known solution");
}

void exactWarmStartConvergesDuringInitialization() {
  auto package = makeLeftPartition();
  package.initial_x = {5.0, 5.0};
  mcpd4::ResidentLinearPartition partition(std::move(package));
  const auto initialized = partition.initialize({5.0});
  requireNear(initialized.residual_norm_squared, 0.0, 1e-24,
              "exact warm start should have zero residual");
  requireNear(initialized.residual_preconditioned_inner, 0.0, 1e-24,
              "exact warm start should have zero preconditioned residual");
  require(initialized.boundary_direction == std::vector<double>({0.0}),
          "zero local residual should still expose a zero boundary direction");
  (void)partition.multiply({0.0});
}

void locallyExactPartitionRemainsInGlobalPcg() {
  mcpd4::LinearSystemPartition left_package;
  left_package.partition_id = 0;
  left_package.owned_global_nodes = {0};
  left_package.ghost_global_nodes = {1};
  left_package.row_offsets = {0, 2};
  left_package.column_indices = {0, 1};
  left_package.values = {2.0, -1.0};
  left_package.rhs = {1.0};
  left_package.initial_x = {0.5};
  left_package.boundary_owned_local_indices = {0};

  mcpd4::LinearSystemPartition right_package;
  right_package.partition_id = 1;
  right_package.owned_global_nodes = {1};
  right_package.ghost_global_nodes = {0};
  right_package.row_offsets = {0, 2};
  right_package.column_indices = {0, 1};
  right_package.values = {2.0, -1.0};
  right_package.rhs = {0.0};
  right_package.initial_x = {0.0};
  right_package.boundary_owned_local_indices = {0};

  mcpd4::ResidentLinearPartition left(std::move(left_package));
  mcpd4::ResidentLinearPartition right(std::move(right_package));
  auto left_initial = left.initialize({0.0});
  auto right_initial = right.initialize({0.5});
  requireNear(left_initial.residual_norm_squared, 0.0, 0.0,
              "left partition should begin locally exact");
  require(left_initial.boundary_direction == std::vector<double>({0.0}),
          "locally exact partition must contribute a zero direction");

  const double rhs_norm_squared = left_initial.rhs_norm_squared +
                                  right_initial.rhs_norm_squared;
  double residual_inner = left_initial.residual_preconditioned_inner +
                          right_initial.residual_preconditioned_inner;
  double relative_residual = std::sqrt(
      (left_initial.residual_norm_squared +
       right_initial.residual_norm_squared) /
      rhs_norm_squared);
  std::vector<double> left_boundary = left_initial.boundary_direction;
  std::vector<double> right_boundary = right_initial.boundary_direction;
  int iterations = 0;
  while (relative_residual > 1e-12 && iterations < 5) {
    const auto left_product = left.multiply({right_boundary.at(0)});
    const auto right_product = right.multiply({left_boundary.at(0)});
    const double denominator = left_product.direction_product_inner +
                               right_product.direction_product_inner;
    const double alpha = residual_inner / denominator;
    const auto left_update = left.updateAlpha(alpha);
    const auto right_update = right.updateAlpha(alpha);
    relative_residual = std::sqrt(
        (left_update.residual_norm_squared +
         right_update.residual_norm_squared) /
        rhs_norm_squared);
    ++iterations;
    if (relative_residual <= 1e-12) {
      break;
    }
    const double next_inner =
        left_update.residual_preconditioned_inner +
        right_update.residual_preconditioned_inner;
    const double beta = next_inner / residual_inner;
    left_boundary = left.updateBeta(beta).boundary_direction;
    right_boundary = right.updateBeta(beta).boundary_direction;
    residual_inner = next_inner;
  }
  require(relative_residual <= 1e-12,
          "global PCG should activate and converge the locally exact side");
  requireNear(left.solution().at(0), 2.0 / 3.0, 1e-12,
              "left global solution mismatch");
  requireNear(right.solution().at(0), 1.0 / 3.0, 1e-12,
              "right global solution mismatch");
}

void validatesPackagesAndStateTransitions() {
  auto package = makeLeftPartition();
  package.row_offsets.pop_back();
  requireThrows(
      [&] { mcpd4::ResidentLinearPartition invalid(std::move(package)); },
      "row offset count must be validated");

  package = makeLeftPartition();
  package.partition_id = -1;
  requireThrows(
      [&] { mcpd4::ResidentLinearPartition invalid(std::move(package)); },
      "negative partition id must be rejected");

  package = makeLeftPartition();
  package.ghost_global_nodes = {1};
  requireThrows(
      [&] { mcpd4::ResidentLinearPartition invalid(std::move(package)); },
      "owned and ghost nodes must be disjoint");

  package = makeLeftPartition();
  package.column_indices = {1, 0, 0, 1, 2};
  requireThrows(
      [&] { mcpd4::ResidentLinearPartition invalid(std::move(package)); },
      "unsorted CSR columns must be rejected");

  package = makeLeftPartition();
  package.column_indices.back() = 3;
  requireThrows(
      [&] { mcpd4::ResidentLinearPartition invalid(std::move(package)); },
      "out-of-range CSR columns must be rejected");

  package = makeLeftPartition();
  package.values.at(0) = std::numeric_limits<double>::quiet_NaN();
  requireThrows(
      [&] { mcpd4::ResidentLinearPartition invalid(std::move(package)); },
      "non-finite matrix values must be rejected");

  package = makeLeftPartition();
  package.values.at(0) = 0.0;
  requireThrows(
      [&] { mcpd4::ResidentLinearPartition invalid(std::move(package)); },
      "non-SPD owned blocks must be rejected");

  package = makeLeftPartition();
  package.boundary_owned_local_indices = {1, 1};
  requireThrows(
      [&] { mcpd4::ResidentLinearPartition invalid(std::move(package)); },
      "duplicate boundary indices must be rejected");

  mcpd4::ResidentLinearPartition partition(makeRightPartition());
  requireThrows([&] { (void)partition.solution(); },
                "solution before initialize must fail");
  requireThrows([&] { (void)partition.multiply({0.0}); },
                "multiply before initialize must fail");
  requireThrows([&] { (void)partition.initialize({}); },
                "ghost vector size must be validated");
  (void)partition.initialize({0.0});
  requireThrows([&] { (void)partition.updateAlpha(
                         std::numeric_limits<double>::infinity()); },
                "non-finite alpha must be rejected");
  requireThrows([&] { (void)partition.updateBeta(
                         std::numeric_limits<double>::quiet_NaN()); },
                "non-finite beta must be rejected");
  requireThrows([&] { (void)partition.multiply({}); },
                "multiply ghost vector size must be validated");
  (void)partition.multiply({0.0});
  requireThrows([&] { (void)partition.multiply({0.0}); },
                "a product cannot be computed twice");
  requireThrows([&] { (void)partition.updateBeta(0.0); },
                "beta cannot be applied before alpha");
  (void)partition.updateAlpha(0.1);
  requireThrows([&] { (void)partition.updateAlpha(0.1); },
                "alpha cannot be applied twice");
  requireThrows([&] { (void)partition.initialize({0.0}); },
                "a resident solve cannot be initialized twice");
  (void)partition.updateBeta(0.0);
}

} // namespace

int main() {
  try {
    splitPcgRecoversKnownSolution();
    exactWarmStartConvergesDuringInitialization();
    locallyExactPartitionRemainsInGlobalPcg();
    validatesPackagesAndStateTransitions();
  } catch (const std::exception &e) {
    std::cerr << "linear_worker_test failed: " << e.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
