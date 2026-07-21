#pragma once

#include <decomp/dualdecomp.h>
#include <decomp/partition_coordinator.h>
#include <decomp/solver_policy.h>

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace mcpd4 {

enum class SolverRegularizationScheme {
  NONE,
  SCALED_EPSILON,
  DISAGREEMENT_PLATEAU_EPSILON,
};

// Algorithm policy shared by the in-process and transported MCPD4 front ends.
// Transport, worker placement, telemetry, and storage location stay outside it.
struct SolverPolicy {
  int max_iteration_count = mcpd3::kDefaultMaxIterationCount;
  long max_total_iteration_count = 0;
  int num_optimization_scales = mcpd3::kDefaultOptimizationScaleCount;
  long initial_step_size = mcpd3::kDefaultInitialStepSize;
  long objective_scale = mcpd3::kDefaultObjectiveScale;
  int patience = mcpd3::kDefaultPatience;
  int disagreement_patience = mcpd3::kDefaultDisagreementPatience;
  bool legacy_patience = false;
  bool exhaust_scale_iterations = false;
  bool exhaust_regularized_scale_iterations = false;
  bool retry_exhaust_regularized_scale_iterations =
      mcpd3::kDefaultRetryExhaustRegularizedScaleIterations;
  long min_step_size = 1;
  bool use_momentum = mcpd3::kDefaultUseMomentum;
  bool enable_group_stopping = mcpd3::kDefaultEnableGroupStopping;
  bool saturate_capacity_overflow = false;
  SolverRegularizationScheme regularization_scheme =
      SolverRegularizationScheme::SCALED_EPSILON;
  int scaled_epsilon_max_step_size =
      mcpd3::kDefaultScaledEpsilonMaxStepSize;
  int scaled_epsilon_strength_cap =
      mcpd3::kDefaultScaledEpsilonStrengthCap;
  mcpd3::Objective regularization_budget_limit = 0;
  bool promote_objective_scale_on_overbudget = true;
  int max_objective_scale_promotions =
      mcpd3::kDefaultMaxObjectiveScalePromotions;
  bool retry_unit_step_without_momentum = false;
  bool randomize_initial_alphas = false;
  long initial_alpha_random_radius = 0;
  unsigned int initial_alpha_random_seed = 0;
  int halo_depth = 1;
  mcpd3::CanonicalCutSelection canonical_cut_selection =
      mcpd3::CanonicalCutSelection::SOLVER_DEFAULT;
  bool force_full_mincut_recompute = false;
};

inline bool usesRegularization(const SolverPolicy &policy) {
  return policy.regularization_scheme != SolverRegularizationScheme::NONE;
}

inline void validateSolverPolicy(const SolverPolicy &policy) {
  if (policy.max_iteration_count <= 0 ||
      policy.num_optimization_scales <= 0 || policy.initial_step_size <= 0 ||
      policy.objective_scale <= 0 || policy.min_step_size <= 0) {
    throw std::invalid_argument(
        "solver iteration, scale, and step values must be positive");
  }
  if (policy.max_total_iteration_count < 0 || policy.patience < 0 ||
      policy.scaled_epsilon_strength_cap < 0 ||
      policy.regularization_budget_limit < 0 ||
      policy.max_objective_scale_promotions < 0 ||
      policy.initial_alpha_random_radius < 0) {
    throw std::invalid_argument("solver nonnegative values must be nonnegative");
  }
  if (policy.scaled_epsilon_max_step_size <= 0) {
    throw std::invalid_argument(
        "scaled-epsilon maximum step size must be positive");
  }
  if (policy.regularization_scheme ==
          SolverRegularizationScheme::DISAGREEMENT_PLATEAU_EPSILON &&
      policy.disagreement_patience <= 0) {
    throw std::invalid_argument(
        "plateau regularization requires positive disagreement patience");
  }
  if (policy.halo_depth != mcpd3::kInfiniteHaloDepth &&
      policy.halo_depth < 1) {
    throw std::invalid_argument("halo depth must be positive or infinite");
  }
}

inline mcpd3::PartitionWorkerRegularizationScheme
coordinatorRegularizationScheme(SolverRegularizationScheme scheme) {
  switch (scheme) {
  case SolverRegularizationScheme::NONE:
    return mcpd3::PartitionWorkerRegularizationScheme::NONE;
  case SolverRegularizationScheme::SCALED_EPSILON:
    return mcpd3::PartitionWorkerRegularizationScheme::SCALED_EPSILON;
  case SolverRegularizationScheme::DISAGREEMENT_PLATEAU_EPSILON:
    return mcpd3::PartitionWorkerRegularizationScheme::
        DISAGREEMENT_PLATEAU_EPSILON;
  }
  throw std::invalid_argument("unknown regularization scheme");
}

inline mcpd3::PartitionWorkerCoordinatorOptions
makeCoordinatorOptions(const SolverPolicy &policy) {
  validateSolverPolicy(policy);
  mcpd3::PartitionWorkerCoordinatorOptions options;
  options.max_iteration_count = policy.max_iteration_count;
  options.max_total_iteration_count = policy.max_total_iteration_count;
  options.num_optimization_scales = policy.num_optimization_scales;
  options.initial_step_size = policy.initial_step_size;
  options.objective_scale = policy.objective_scale;
  options.patience = policy.patience;
  options.disagreement_patience = policy.disagreement_patience;
  options.legacy_patience = policy.legacy_patience;
  options.exhaust_scale_iterations = policy.exhaust_scale_iterations;
  options.exhaust_regularized_scale_iterations =
      policy.exhaust_regularized_scale_iterations;
  options.min_step_size = policy.min_step_size;
  options.use_momentum = policy.use_momentum;
  options.enable_group_stopping = policy.enable_group_stopping;
  options.saturate_capacity_overflow = policy.saturate_capacity_overflow;
  options.regularization_scheme =
      coordinatorRegularizationScheme(policy.regularization_scheme);
  options.scaled_epsilon_max_step_size =
      policy.scaled_epsilon_max_step_size;
  options.scaled_epsilon_strength_cap = policy.scaled_epsilon_strength_cap;
  options.regularization_budget_limit = policy.regularization_budget_limit;
  options.promote_objective_scale_on_overbudget =
      policy.promote_objective_scale_on_overbudget;
  options.max_objective_scale_promotions =
      policy.max_objective_scale_promotions;
  options.retry_unit_step_without_momentum =
      policy.retry_unit_step_without_momentum;
  options.randomize_initial_alphas = policy.randomize_initial_alphas;
  options.initial_alpha_random_radius = policy.initial_alpha_random_radius;
  options.initial_alpha_random_seed = policy.initial_alpha_random_seed;
  return options;
}

inline mcpd3::DualDecompositionOptions makePackageOptions(
    const SolverPolicy &policy,
    mcpd3::SolverStorageOptions storage = {}) {
  validateSolverPolicy(policy);
  mcpd3::DualDecompositionOptions options;
  options.track_primal_upper_bound = false;
  options.verbose = false;
  options.thread_count = 1;
  options.objective_scale = policy.objective_scale;
  options.saturate_capacity_overflow = policy.saturate_capacity_overflow;
  options.construct_solvers = false;
  options.emit_partition_packages = true;
  options.materialize_all_partition_nodes = true;
  options.halo_depth = policy.halo_depth;
  options.canonical_cut_selection = policy.canonical_cut_selection;
  options.force_full_mincut_recompute = policy.force_full_mincut_recompute;
  options.solver_storage = std::move(storage);
  return options;
}

inline int scheduleLevelsThroughUnit(long initial_step_size) {
  if (initial_step_size <= 0) {
    throw std::invalid_argument("initial step size must be positive");
  }
  int levels = 0;
  for (long step = initial_step_size;; step = std::max(1L, step / 10)) {
    ++levels;
    if (step == 1) {
      return levels;
    }
  }
}

inline void configureCoordinatorSchedule(
    mcpd3::PartitionWorkerCoordinator *coordinator,
    const SolverPolicy &policy,
    bool exhaust_regularized_scale_iterations) {
  if (coordinator == nullptr) {
    throw std::invalid_argument("coordinator is required");
  }
  long initial_step_size = policy.initial_step_size;
  int num_optimization_scales = policy.num_optimization_scales;
  if (coordinator->objectiveScale() > policy.objective_scale) {
    initial_step_size =
        std::max(initial_step_size, coordinator->configuredInitialStepSize());
    num_optimization_scales =
        std::max(num_optimization_scales,
                 scheduleLevelsThroughUnit(initial_step_size));
  }
  coordinator->configureOptimizationSchedule(
      num_optimization_scales, initial_step_size,
      exhaust_regularized_scale_iterations);
}

} // namespace mcpd4
