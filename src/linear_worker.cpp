#include <mcpd4/linear_worker.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <utility>

namespace mcpd4 {
namespace {

double innerProduct(const std::vector<double> &left,
                    const std::vector<double> &right) {
  double result = 0.0;
  for (std::size_t i = 0; i < left.size(); ++i) {
    result += left[i] * right[i];
  }
  return result;
}

void requireFinite(const std::vector<double> &values, const char *name) {
  if (!std::all_of(values.begin(), values.end(),
                   [](double value) { return std::isfinite(value); })) {
    throw std::invalid_argument(std::string(name) +
                                " must contain only finite values");
  }
}

} // namespace

ResidentLinearPartition::ResidentLinearPartition(
    LinearSystemPartition partition)
    : partition_(std::move(partition)), x_(partition_.initial_x) {
  validate();
  factorizeOwnedBlock();
}

void ResidentLinearPartition::validate() const {
  const std::size_t owned = partition_.owned_global_nodes.size();
  const std::size_t ghosts = partition_.ghost_global_nodes.size();
  if (partition_.partition_id < 0) {
    throw std::invalid_argument("partition id must be non-negative");
  }
  if (owned == 0) {
    throw std::invalid_argument("linear partition must own at least one row");
  }
  if (partition_.row_offsets.size() != owned + 1 ||
      partition_.row_offsets.front() != 0) {
    throw std::invalid_argument("CSR row offsets do not match owned rows");
  }
  if (!std::is_sorted(partition_.row_offsets.begin(),
                      partition_.row_offsets.end()) ||
      partition_.row_offsets.back() != partition_.column_indices.size() ||
      partition_.column_indices.size() != partition_.values.size()) {
    throw std::invalid_argument("CSR arrays have inconsistent lengths");
  }
  if (partition_.rhs.size() != owned ||
      partition_.initial_x.size() != owned) {
    throw std::invalid_argument("owned vector sizes do not match row count");
  }
  requireFinite(partition_.values, "matrix values");
  requireFinite(partition_.rhs, "right-hand side");
  requireFinite(partition_.initial_x, "initial solution");

  std::unordered_set<int> global_nodes;
  for (int node : partition_.owned_global_nodes) {
    if (node < 0 || !global_nodes.insert(node).second) {
      throw std::invalid_argument("owned global nodes must be unique");
    }
  }
  for (int node : partition_.ghost_global_nodes) {
    if (node < 0 || !global_nodes.insert(node).second) {
      throw std::invalid_argument(
          "ghost global nodes must be unique and disjoint from owned nodes");
    }
  }

  const std::size_t column_count = owned + ghosts;
  for (std::size_t row = 0; row < owned; ++row) {
    int previous_column = -1;
    int diagonal_count = 0;
    for (std::uint64_t offset = partition_.row_offsets[row];
         offset < partition_.row_offsets[row + 1]; ++offset) {
      const int column = partition_.column_indices[offset];
      if (column < 0 || static_cast<std::size_t>(column) >= column_count) {
        throw std::invalid_argument("CSR column index is out of range");
      }
      if (column <= previous_column) {
        throw std::invalid_argument(
            "CSR columns must be strictly increasing within each row");
      }
      previous_column = column;
      if (column == static_cast<int>(row)) {
        ++diagonal_count;
      }
    }
    if (diagonal_count != 1) {
      throw std::invalid_argument("every owned row must have one diagonal");
    }
  }

  std::unordered_set<int> boundary_indices;
  for (int index : partition_.boundary_owned_local_indices) {
    if (index < 0 || static_cast<std::size_t>(index) >= owned ||
        !boundary_indices.insert(index).second) {
      throw std::invalid_argument(
          "boundary owned indices must be unique and in range");
    }
  }
}

void ResidentLinearPartition::factorizeOwnedBlock() {
  const std::size_t owned = partition_.owned_global_nodes.size();
  factor_rows_.assign(owned, {});
  for (std::size_t row = 0; row < owned; ++row) {
    double diagonal = std::numeric_limits<double>::quiet_NaN();
    auto &factor_row = factor_rows_[row];
    for (std::uint64_t offset = partition_.row_offsets[row];
         offset < partition_.row_offsets[row + 1]; ++offset) {
      const int column = partition_.column_indices[offset];
      if (column < static_cast<int>(row)) {
        double value = partition_.values[offset];
        const auto &other_row = factor_rows_[column];
        std::size_t left = 0;
        std::size_t right = 0;
        while (left < factor_row.size() && right < other_row.size()) {
          const int left_column = factor_row[left].column;
          const int right_column = other_row[right].column;
          if (left_column >= column || right_column >= column) {
            break;
          }
          if (left_column == right_column) {
            value -= factor_row[left].value * other_row[right].value;
            ++left;
            ++right;
          } else if (left_column < right_column) {
            ++left;
          } else {
            ++right;
          }
        }
        const double other_diagonal = other_row.back().value;
        value /= other_diagonal;
        if (!std::isfinite(value)) {
          throw std::invalid_argument(
              "IC(0) factorization produced a non-finite value");
        }
        factor_row.push_back({column, value});
      } else if (column == static_cast<int>(row)) {
        diagonal = partition_.values[offset];
      }
    }
    for (const FactorEntry &entry : factor_row) {
      diagonal -= entry.value * entry.value;
    }
    if (!(diagonal > 0.0) || !std::isfinite(diagonal)) {
      throw std::invalid_argument("owned matrix block is not IC(0)-factorable SPD");
    }
    factor_row.push_back(
        {static_cast<int>(row), std::sqrt(diagonal)});
  }
}

std::vector<double> ResidentLinearPartition::multiplyVector(
    const std::vector<double> &owned_values,
    const std::vector<double> &ghost_values) const {
  const std::size_t owned = partition_.owned_global_nodes.size();
  if (owned_values.size() != owned ||
      ghost_values.size() != partition_.ghost_global_nodes.size()) {
    throw std::invalid_argument("linear multiply vector size mismatch");
  }
  requireFinite(owned_values, "owned vector");
  requireFinite(ghost_values, "ghost vector");
  std::vector<double> result(owned, 0.0);
  for (std::size_t row = 0; row < owned; ++row) {
    for (std::uint64_t offset = partition_.row_offsets[row];
         offset < partition_.row_offsets[row + 1]; ++offset) {
      const int column = partition_.column_indices[offset];
      const double vector_value =
          static_cast<std::size_t>(column) < owned
              ? owned_values[column]
              : ghost_values[column - static_cast<int>(owned)];
      result[row] += partition_.values[offset] * vector_value;
    }
  }
  requireFinite(result, "matrix product");
  return result;
}

std::vector<double> ResidentLinearPartition::applyPreconditioner(
    const std::vector<double> &residual) const {
  std::vector<double> result(residual);
  for (std::size_t row = 0; row < factor_rows_.size(); ++row) {
    const auto &entries = factor_rows_[row];
    for (std::size_t offset = 0; offset + 1 < entries.size(); ++offset) {
      result[row] -= entries[offset].value * result[entries[offset].column];
    }
    result[row] /= entries.back().value;
  }
  for (std::size_t reverse_row = factor_rows_.size(); reverse_row-- > 0;) {
    const auto &entries = factor_rows_[reverse_row];
    result[reverse_row] /= entries.back().value;
    for (std::size_t offset = 0; offset + 1 < entries.size(); ++offset) {
      result[entries[offset].column] -=
          entries[offset].value * result[reverse_row];
    }
  }
  requireFinite(result, "preconditioned residual");
  return result;
}

std::vector<double> ResidentLinearPartition::boundaryValues(
    const std::vector<double> &values) const {
  std::vector<double> result;
  result.reserve(partition_.boundary_owned_local_indices.size());
  for (int index : partition_.boundary_owned_local_indices) {
    result.push_back(values[index]);
  }
  return result;
}

LinearInitializeResult ResidentLinearPartition::initialize(
    const std::vector<double> &ghost_x) {
  if (state_ != State::Constructed) {
    throw std::logic_error("linear partition is already initialized");
  }
  const std::vector<double> product = multiplyVector(x_, ghost_x);
  residual_.resize(partition_.rhs.size());
  for (std::size_t i = 0; i < residual_.size(); ++i) {
    residual_[i] = partition_.rhs[i] - product[i];
  }
  preconditioned_residual_ = applyPreconditioner(residual_);
  const double residual_norm_squared = innerProduct(residual_, residual_);
  const double residual_preconditioned_inner =
      innerProduct(residual_, preconditioned_residual_);
  if (residual_norm_squared == 0.0) {
    direction_.clear();
    state_ = State::Converged;
  } else {
    if (!(residual_preconditioned_inner > 0.0) ||
        !std::isfinite(residual_preconditioned_inner)) {
      throw std::runtime_error(
          "preconditioner did not produce a positive residual inner product");
    }
    direction_ = preconditioned_residual_;
    state_ = State::DirectionReady;
  }
  return {innerProduct(partition_.rhs, partition_.rhs),
          residual_norm_squared, residual_preconditioned_inner,
          state_ == State::DirectionReady ? boundaryValues(direction_)
                                           : std::vector<double>{}};
}

LinearMultiplyResult ResidentLinearPartition::multiply(
    const std::vector<double> &ghost_direction) {
  if (state_ != State::DirectionReady) {
    throw std::logic_error("linear direction is not ready for multiplication");
  }
  product_ = multiplyVector(direction_, ghost_direction);
  state_ = State::ProductReady;
  return {innerProduct(direction_, product_)};
}

LinearAlphaUpdateResult ResidentLinearPartition::updateAlpha(double alpha) {
  if (!std::isfinite(alpha)) {
    throw std::invalid_argument("PCG alpha must be finite");
  }
  if (state_ != State::ProductReady) {
    throw std::logic_error("linear product is not ready for alpha update");
  }
  for (std::size_t i = 0; i < x_.size(); ++i) {
    x_[i] += alpha * direction_[i];
    residual_[i] -= alpha * product_[i];
  }
  requireFinite(x_, "updated solution");
  requireFinite(residual_, "updated residual");
  preconditioned_residual_ = applyPreconditioner(residual_);
  const double residual_norm_squared = innerProduct(residual_, residual_);
  const double residual_preconditioned_inner =
      innerProduct(residual_, preconditioned_residual_);
  product_.clear();
  if (residual_norm_squared == 0.0) {
    direction_.clear();
    state_ = State::Converged;
  } else {
    if (!(residual_preconditioned_inner > 0.0) ||
        !std::isfinite(residual_preconditioned_inner)) {
      throw std::runtime_error(
          "preconditioner did not preserve a positive residual inner product");
    }
    state_ = State::AlphaUpdated;
  }
  return {residual_norm_squared, residual_preconditioned_inner};
}

LinearBetaUpdateResult ResidentLinearPartition::updateBeta(double beta) {
  if (!std::isfinite(beta)) {
    throw std::invalid_argument("PCG beta must be finite");
  }
  if (state_ != State::AlphaUpdated) {
    throw std::logic_error("alpha update is not ready for beta update");
  }
  for (std::size_t i = 0; i < direction_.size(); ++i) {
    direction_[i] = preconditioned_residual_[i] + beta * direction_[i];
  }
  requireFinite(direction_, "updated direction");
  state_ = State::DirectionReady;
  return {boundaryValues(direction_)};
}

const std::vector<double> &ResidentLinearPartition::solution() const {
  if (state_ == State::Constructed) {
    throw std::logic_error("linear partition has not been initialized");
  }
  return x_;
}

} // namespace mcpd4
