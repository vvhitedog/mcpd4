#include <mcpd4/solver_policy.h>

#include <stdexcept>
#include <string>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Function>
void requireThrows(Function function, const std::string &message) {
  try {
    function();
  } catch (const std::invalid_argument &) {
    return;
  }
  throw std::runtime_error(message);
}

void productionDefaultsMatchNativeProductProfile() {
  const mcpd4::SolverPolicy policy;
  require(policy.max_iteration_count == mcpd3::kDefaultMaxIterationCount,
          "maximum iteration default drifted");
  require(policy.num_optimization_scales ==
              mcpd3::kDefaultOptimizationScaleCount,
          "scale-count default drifted");
  require(policy.initial_step_size == mcpd3::kDefaultInitialStepSize,
          "initial-step default drifted");
  require(policy.objective_scale == mcpd3::kDefaultObjectiveScale,
          "objective-scale default drifted");
  require(policy.patience == mcpd3::kDefaultPatience,
          "patience default drifted");
  require(policy.disagreement_patience ==
              mcpd3::kDefaultDisagreementPatience,
          "disagreement-patience default drifted");
  require(policy.use_momentum == mcpd3::kDefaultUseMomentum,
          "momentum default drifted");
  require(policy.enable_group_stopping ==
              mcpd3::kDefaultEnableGroupStopping,
          "group-stopping default drifted");
  require(policy.scaled_epsilon_max_step_size ==
              mcpd3::kDefaultScaledEpsilonMaxStepSize,
          "regularization cutoff default drifted");
  require(policy.scaled_epsilon_strength_cap ==
              mcpd3::kDefaultScaledEpsilonStrengthCap,
          "regularization cap default drifted");
  require(policy.max_objective_scale_promotions ==
              mcpd3::kDefaultMaxObjectiveScalePromotions,
          "promotion default drifted");
  require(policy.retry_exhaust_regularized_scale_iterations,
          "product policy must retry exhausted regularized scales");
}

void everyCoordinatorFieldIsTranslated() {
  mcpd4::SolverPolicy policy;
  policy.max_iteration_count = 101;
  policy.max_total_iteration_count = 202;
  policy.num_optimization_scales = 3;
  policy.initial_step_size = 303;
  policy.objective_scale = 404;
  policy.patience = 5;
  policy.disagreement_patience = 6;
  policy.legacy_patience = true;
  policy.exhaust_scale_iterations = true;
  policy.exhaust_regularized_scale_iterations = true;
  policy.min_step_size = 2;
  policy.use_momentum = false;
  policy.enable_group_stopping = true;
  policy.saturate_capacity_overflow = true;
  policy.regularization_scheme =
      mcpd4::SolverRegularizationScheme::DISAGREEMENT_PLATEAU_EPSILON;
  policy.scaled_epsilon_max_step_size = 7;
  policy.scaled_epsilon_strength_cap = 8;
  policy.regularization_budget_limit = 909;
  policy.promote_objective_scale_on_overbudget = false;
  policy.max_objective_scale_promotions = 10;
  policy.retry_unit_step_without_momentum = true;
  policy.randomize_initial_alphas = true;
  policy.initial_alpha_random_radius = 11;
  policy.initial_alpha_random_seed = 12;

  const auto options = mcpd4::makeCoordinatorOptions(policy);
  require(options.max_iteration_count == 101 &&
              options.max_total_iteration_count == 202 &&
              options.num_optimization_scales == 3 &&
              options.initial_step_size == 303 &&
              options.objective_scale == 404 && options.patience == 5 &&
              options.disagreement_patience == 6 &&
              options.legacy_patience && options.exhaust_scale_iterations &&
              options.exhaust_regularized_scale_iterations &&
              options.min_step_size == 2 && !options.use_momentum &&
              options.enable_group_stopping &&
              options.saturate_capacity_overflow,
          "coordinator schedule translation is incomplete");
  require(options.regularization_scheme ==
              mcpd3::PartitionWorkerRegularizationScheme::
                  DISAGREEMENT_PLATEAU_EPSILON &&
              options.scaled_epsilon_max_step_size == 7 &&
              options.scaled_epsilon_strength_cap == 8 &&
              options.regularization_budget_limit == 909 &&
              !options.promote_objective_scale_on_overbudget &&
              options.max_objective_scale_promotions == 10 &&
              options.retry_unit_step_without_momentum &&
              options.randomize_initial_alphas &&
              options.initial_alpha_random_radius == 11 &&
              options.initial_alpha_random_seed == 12,
          "coordinator regularization translation is incomplete");
}

void regularizationBranchesTranslateExactly() {
  mcpd4::SolverPolicy policy;
  policy.regularization_scheme = mcpd4::SolverRegularizationScheme::NONE;
  require(mcpd4::makeCoordinatorOptions(policy).regularization_scheme ==
              mcpd3::PartitionWorkerRegularizationScheme::NONE &&
              !mcpd4::usesRegularization(policy),
          "none regularization branch drifted");
  policy.regularization_scheme =
      mcpd4::SolverRegularizationScheme::SCALED_EPSILON;
  require(mcpd4::makeCoordinatorOptions(policy).regularization_scheme ==
              mcpd3::PartitionWorkerRegularizationScheme::SCALED_EPSILON &&
              mcpd4::usesRegularization(policy),
          "scaled-epsilon branch drifted");
  policy.regularization_scheme =
      mcpd4::SolverRegularizationScheme::DISAGREEMENT_PLATEAU_EPSILON;
  require(mcpd4::makeCoordinatorOptions(policy).regularization_scheme ==
              mcpd3::PartitionWorkerRegularizationScheme::
                  DISAGREEMENT_PLATEAU_EPSILON,
          "plateau-epsilon branch drifted");
}

void packagePolicyMaterializesTheSameProblem() {
  mcpd4::SolverPolicy policy;
  policy.objective_scale = 77;
  policy.halo_depth = 3;
  policy.saturate_capacity_overflow = true;
  policy.canonical_cut_selection =
      mcpd3::CanonicalCutSelection::MAXIMUM_LABELS;
  policy.force_full_mincut_recompute = true;

  const auto options = mcpd4::makePackageOptions(policy);
  require(!options.track_primal_upper_bound && !options.verbose &&
              options.thread_count == 1 && options.objective_scale == 77 &&
              !options.construct_solvers && options.emit_partition_packages &&
              options.materialize_all_partition_nodes &&
              options.saturate_capacity_overflow && options.halo_depth == 3 &&
              options.canonical_cut_selection ==
                  mcpd3::CanonicalCutSelection::MAXIMUM_LABELS &&
              options.force_full_mincut_recompute,
          "package-source behavior must match native problem construction");
}

void invalidPoliciesAreRejectedAcrossBranches() {
  mcpd4::SolverPolicy policy;
  policy.objective_scale = 0;
  requireThrows([&] { mcpd4::validateSolverPolicy(policy); },
                "zero objective scale must fail");
  policy = {};
  policy.scaled_epsilon_strength_cap = -1;
  requireThrows([&] { mcpd4::validateSolverPolicy(policy); },
                "negative regularization cap must fail");
  policy = {};
  policy.regularization_scheme =
      mcpd4::SolverRegularizationScheme::DISAGREEMENT_PLATEAU_EPSILON;
  policy.disagreement_patience = 0;
  requireThrows([&] { mcpd4::validateSolverPolicy(policy); },
                "plateau mode requires positive disagreement patience");
  policy = {};
  policy.halo_depth = 0;
  requireThrows([&] { mcpd4::validateSolverPolicy(policy); },
                "zero halo must fail");
}

} // namespace

int main() {
  try {
    productionDefaultsMatchNativeProductProfile();
    everyCoordinatorFieldIsTranslated();
    regularizationBranchesTranslateExactly();
    packagePolicyMaterializesTheSameProblem();
    invalidPoliciesAreRejectedAcrossBranches();
  } catch (const std::exception &error) {
    throw std::runtime_error(std::string("solver policy test failed: ") +
                             error.what());
  }
  return 0;
}
