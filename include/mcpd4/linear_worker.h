#pragma once

#include <cstdint>
#include <vector>

namespace mcpd4 {

// CSR columns address owned nodes first, followed by ghost nodes.
struct LinearSystemPartition {
  int partition_id = -1;
  std::vector<int> owned_global_nodes;
  std::vector<int> ghost_global_nodes;
  std::vector<std::uint64_t> row_offsets;
  std::vector<int> column_indices;
  std::vector<double> values;
  std::vector<double> rhs;
  std::vector<double> initial_x;
  std::vector<int> boundary_owned_local_indices;
};

struct LinearInitializeResult {
  double rhs_norm_squared = 0.0;
  double residual_norm_squared = 0.0;
  double residual_preconditioned_inner = 0.0;
  std::vector<double> boundary_direction;
};

struct LinearMultiplyResult {
  double direction_product_inner = 0.0;
};

struct LinearAlphaUpdateResult {
  double residual_norm_squared = 0.0;
  double residual_preconditioned_inner = 0.0;
};

struct LinearBetaUpdateResult {
  std::vector<double> boundary_direction;
};

class ResidentLinearPartition {
public:
  explicit ResidentLinearPartition(LinearSystemPartition partition);

  LinearInitializeResult initialize(const std::vector<double> &ghost_x);
  LinearMultiplyResult multiply(
      const std::vector<double> &ghost_direction);
  LinearAlphaUpdateResult updateAlpha(double alpha);
  LinearBetaUpdateResult updateBeta(double beta);

  const std::vector<double> &solution() const;

private:
  struct FactorEntry {
    int column = -1;
    double value = 0.0;
  };

  enum class State {
    Constructed,
    DirectionReady,
    ProductReady,
    AlphaUpdated,
  };

  void validate() const;
  void factorizeOwnedBlock();
  std::vector<double> multiplyVector(
      const std::vector<double> &owned,
      const std::vector<double> &ghost) const;
  std::vector<double> applyPreconditioner(
      const std::vector<double> &residual) const;
  std::vector<double> boundaryValues(const std::vector<double> &values) const;

  LinearSystemPartition partition_;
  std::vector<std::vector<FactorEntry>> factor_rows_;
  std::vector<double> x_;
  std::vector<double> residual_;
  std::vector<double> preconditioned_residual_;
  std::vector<double> direction_;
  std::vector<double> product_;
  State state_ = State::Constructed;
};

} // namespace mcpd4
