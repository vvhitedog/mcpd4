#include <mcpd4/shared_partition_worker.h>

#include <limits>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace mcpd4 {

struct SharedPartitionWorkerConnection::State {
  explicit State(std::unique_ptr<mcpd3::PartitionWorker> worker_value)
      : worker(std::move(worker_value)) {
    if (!worker) {
      throw std::invalid_argument("shared partition worker must not be null");
    }
  }

  std::mutex mutex;
  std::unique_ptr<mcpd3::PartitionWorker> worker;
  int next_remote_partition_id = 0;
};

namespace {

class NamespacePartitionWorker final : public mcpd3::PartitionWorker {
public:
  explicit NamespacePartitionWorker(
      std::shared_ptr<SharedPartitionWorkerConnection::State> state)
      : state_(std::move(state)) {}

  mcpd3::PartitionWorkerResourceEstimate resourceEstimate() const override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->worker->resourceEstimate();
  }

  void loadPartition(const mcpd3::PartitionPackage &package) override {
    auto copy = package;
    loadPartition(std::move(copy));
  }

  void loadPartition(mcpd3::PartitionPackage &&package) override {
    const int local_id = package.partition_id;
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (local_to_remote_.find(local_id) != local_to_remote_.end()) {
      throw std::runtime_error("namespace partition id " +
                               std::to_string(local_id) +
                               " is already loaded");
    }
    if (state_->next_remote_partition_id ==
        std::numeric_limits<int>::max()) {
      throw std::overflow_error("remote partition namespace exhausted");
    }
    const int remote_id = state_->next_remote_partition_id++;
    package.partition_id = remote_id;
    state_->worker->loadPartition(std::move(package));
    local_to_remote_.emplace(local_id, remote_id);
    remote_to_local_.emplace(remote_id, local_id);
  }

  mcpd3::PartitionSolveResult solveRound(
      const mcpd3::PartitionSolveRequest &request) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    auto remote_request = request;
    remote_request.partition_id = remoteId(request.partition_id);
    auto result = state_->worker->solveRound(remote_request);
    result.partition_id = localId(result.partition_id);
    return result;
  }

  std::vector<mcpd3::PartitionSolveResult> solveRoundBatch(
      const std::vector<mcpd3::PartitionSolveRequest> &requests) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::vector<mcpd3::PartitionSolveRequest> remote_requests = requests;
    for (auto &request : remote_requests) {
      request.partition_id = remoteId(request.partition_id);
    }
    auto results = state_->worker->solveRoundBatch(remote_requests);
    for (auto &result : results) {
      result.partition_id = localId(result.partition_id);
    }
    return results;
  }

  void scaleObjective(long factor,
                      bool saturate_capacity_overflow = false) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    if (local_to_remote_.empty()) {
      throw std::runtime_error("partition must be loaded before scaleObjective");
    }
    std::vector<int> remote_ids;
    remote_ids.reserve(local_to_remote_.size());
    for (const auto &[local_id, remote_id] : local_to_remote_) {
      (void)local_id;
      remote_ids.push_back(remote_id);
    }
    state_->worker->scaleObjectivePartitions(
        remote_ids, factor, saturate_capacity_overflow);
  }

  void scaleObjectivePartitions(
      const std::vector<int> &partition_ids, long factor,
      bool saturate_capacity_overflow = false) override {
    if (partition_ids.empty()) {
      throw std::runtime_error(
          "objective scaling requires at least one partition id");
    }
    std::lock_guard<std::mutex> lock(state_->mutex);
    std::unordered_set<int> seen;
    std::vector<int> remote_ids;
    remote_ids.reserve(partition_ids.size());
    for (const int local_id : partition_ids) {
      if (!seen.insert(local_id).second) {
        throw std::runtime_error("duplicate objective scale partition id " +
                                 std::to_string(local_id));
      }
      remote_ids.push_back(remoteId(local_id));
    }
    state_->worker->scaleObjectivePartitions(
        remote_ids, factor, saturate_capacity_overflow);
  }

  void replacePartitionCapacities(
      const mcpd3::PartitionCapacityUpdate &update) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->worker->replacePartitionCapacitiesFor(
        remoteId(update.partition_id), update);
  }

  void replacePartitionCapacitiesFor(
      int target_partition_id,
      const mcpd3::PartitionCapacityUpdate &update) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->worker->replacePartitionCapacitiesFor(
        remoteId(target_partition_id), update);
  }

  std::size_t fullLabelCount(int partition_id) const override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    return state_->worker->fullLabelCount(remoteId(partition_id));
  }

  void copyFullLabels(int partition_id, std::size_t offset,
                      mcpd3::NodeLabel *destination,
                      std::size_t count) override {
    std::lock_guard<std::mutex> lock(state_->mutex);
    state_->worker->copyFullLabels(remoteId(partition_id), offset,
                                   destination, count);
  }

private:
  int remoteId(int local_id) const {
    const auto found = local_to_remote_.find(local_id);
    if (found == local_to_remote_.end()) {
      throw std::runtime_error("unknown namespace partition id " +
                               std::to_string(local_id));
    }
    return found->second;
  }

  int localId(int remote_id) const {
    const auto found = remote_to_local_.find(remote_id);
    if (found == remote_to_local_.end()) {
      throw std::runtime_error("worker returned partition outside namespace " +
                               std::to_string(remote_id));
    }
    return found->second;
  }

  std::shared_ptr<SharedPartitionWorkerConnection::State> state_;
  std::unordered_map<int, int> local_to_remote_;
  std::unordered_map<int, int> remote_to_local_;
};

} // namespace

SharedPartitionWorkerConnection::SharedPartitionWorkerConnection(
    std::unique_ptr<mcpd3::PartitionWorker> worker)
    : state_(std::make_shared<State>(std::move(worker))) {}

std::unique_ptr<mcpd3::PartitionWorker>
SharedPartitionWorkerConnection::makeNamespace() {
  return std::make_unique<NamespacePartitionWorker>(state_);
}

} // namespace mcpd4
