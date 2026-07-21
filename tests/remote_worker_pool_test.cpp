#include <mcpd4/discovery.h>
#include <mcpd4/remote_worker_pool.h>
#include <mcpd4/runtime.h>

#include <chrono>
#include <cstdlib>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

namespace {

using namespace std::chrono_literals;

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

void validatesAcceptConfiguration() {
  mcpd4::RemoteWorkerPoolOptions options;
  options.min_worker_count = 0;
  requireThrowsContaining(
      [&] { (void)mcpd4::RemoteWorkerPool::accept(options); },
      "worker count must be positive",
      "remote pool must reject a nonpositive worker count");
  options.min_worker_count = 1;
  options.accept_timeout = 0ms;
  requireThrowsContaining(
      [&] { (void)mcpd4::RemoteWorkerPool::accept(options); },
      "timeout must be positive",
      "remote pool must reject a nonpositive timeout");
}

void discoveryAcceptsAndRetainsWorkerForNamespacedSolve() {
  mcpd4::RemoteWorkerPoolOptions options;
  options.bind_host = "127.0.0.1";
  options.advertise_host = "127.0.0.1";
  options.tcp_port = 0;
  options.discovery_port = 0;
  options.enable_discovery = true;
  options.discovery_token = "remote-pool-test";
  options.min_worker_count = 1;
  options.accept_timeout = 5s;

  std::promise<mcpd4::RemoteWorkerPoolListeningInfo> listening_promise;
  options.on_listening = [&](const auto &info) {
    listening_promise.set_value(info);
  };
  auto accept_future = std::async(std::launch::async, [&] {
    return mcpd4::RemoteWorkerPool::accept(options);
  });
  const auto listening = listening_promise.get_future().get();
  require(listening.tcp_port != 0 && listening.discovery_port != 0,
          "dynamic remote pool ports must be reported");

  const auto discovered = mcpd4::discoverOneCoordinator(
      "127.0.0.1", listening.discovery_port, options.discovery_token, 1s);
  require(discovered.tcp_port == listening.tcp_port,
          "discovery must advertise the active TCP listener");

  std::exception_ptr worker_error;
  std::thread worker_thread([&] {
    try {
      mcpd4::runWorkerClient(
          discovered.host, discovered.tcp_port,
          mcpd4::makeDefaultHello("remote-pool-worker"));
    } catch (...) {
      worker_error = std::current_exception();
    }
  });

  const auto close = mcpd4::closeCoordinatorDiscovery(
      "127.0.0.1", listening.discovery_port, options.discovery_token, 1s);
  require(close.accepted,
          "discovery close must tell the coordinator to start");
  auto remote_pool = accept_future.get();
  require(remote_pool->workerCount() == 1,
          "remote pool must retain the accepted worker");

  auto workers = remote_pool->sharedPool()->makeNamespaceWorkers();
  mcpd3::PartitionPackage package;
  package.partition_id = 0;
  package.local_node_count = 2;
  package.arcs = {0, 1};
  package.arc_capacities = {7, 0};
  package.terminal_capacities = {9, -9};
  package.local_to_global = {0, 1};
  workers.front()->loadPartition(std::move(package));
  mcpd3::PartitionSolveRequest request;
  request.partition_id = 0;
  const auto result = workers.front()->solveRound(request);
  require(result.lower_bound == 7,
          "accepted discovery worker must execute a namespaced min-cut");

  remote_pool->stop(/*reason=*/0, "remote pool test complete");
  worker_thread.join();
  if (worker_error) {
    std::rethrow_exception(worker_error);
  }
}

} // namespace

int main() {
  try {
    validatesAcceptConfiguration();
    discoveryAcceptsAndRetainsWorkerForNamespacedSolve();
  } catch (const std::exception &error) {
    std::cerr << "remote_worker_pool_test failed: " << error.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
