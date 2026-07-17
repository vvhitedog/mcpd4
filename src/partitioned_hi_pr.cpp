#include <mcpd4/partitioned_hi_pr.h>

#include <stdexcept>

namespace mcpd4::experimental {

PartitionedHiPrResult partitionedHiPr(
    int, int, int, const std::vector<DirectedArc> &,
    const std::vector<int> &, const PartitionedHiPrOptions &) {
  throw std::logic_error("partitioned hi_pr experiment is not implemented");
}

} // namespace mcpd4::experimental
