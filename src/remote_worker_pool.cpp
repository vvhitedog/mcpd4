#include <mcpd4/remote_worker_pool.h>

#include <mcpd4/discovery.h>

#include <algorithm>
#include <cerrno>
#include <exception>
#include <poll.h>
#include <stdexcept>
#include <utility>

namespace mcpd4 {
namespace {

void validateOptions(const RemoteWorkerPoolOptions &options) {
  if (options.min_worker_count <= 0) {
    throw std::invalid_argument("remote worker count must be positive");
  }
  if (options.accept_timeout <= std::chrono::milliseconds::zero()) {
    throw std::invalid_argument("remote worker accept timeout must be positive");
  }
  if (options.enable_discovery && options.discovery_token.empty()) {
    throw std::invalid_argument("discovery token must not be empty");
  }
}

} // namespace

std::shared_ptr<RemoteWorkerPool>
RemoteWorkerPool::accept(const RemoteWorkerPoolOptions &options) {
  validateOptions(options);
  auto result = std::shared_ptr<RemoteWorkerPool>(new RemoteWorkerPool());
  auto listener = listenTcp(options.bind_host, options.tcp_port);
  SocketHandle discovery_socket;
  if (options.enable_discovery) {
    discovery_socket =
        bindDiscoveryUdp(options.bind_host, options.discovery_port);
  }
  const RemoteWorkerPoolListeningInfo listening{
      localPort(listener),
      discovery_socket.valid()
          ? localPort(discovery_socket)
          : static_cast<std::uint16_t>(0),
      options.min_worker_count};
  if (options.on_listening) {
    options.on_listening(listening);
  }

  const auto deadline = std::chrono::steady_clock::now() +
                        options.accept_timeout;
  bool discovery_closed = !options.enable_discovery;
  while (static_cast<int>(result->remote_workers_.size()) <
             options.min_worker_count ||
         !discovery_closed) {
    const auto now = std::chrono::steady_clock::now();
    if (now >= deadline) {
      throw std::runtime_error(
          "timed out waiting for remote MCPD4 workers");
    }
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - now);
    const int timeout_ms =
        std::max(1, std::min<int>(250, static_cast<int>(remaining.count())));

    pollfd descriptors[2]{};
    descriptors[0].fd = listener.get();
    descriptors[0].events = POLLIN;
    descriptors[1].fd = discovery_socket.valid() ? discovery_socket.get() : -1;
    descriptors[1].events = POLLIN;
    const int descriptor_count = discovery_socket.valid() ? 2 : 1;
    const int poll_result =
        ::poll(descriptors, descriptor_count, timeout_ms);
    if (poll_result < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw std::runtime_error("poll failed while accepting MCPD4 workers");
    }
    if (poll_result == 0) {
      continue;
    }

    if (discovery_socket.valid() &&
        (descriptors[1].revents & POLLIN) != 0) {
      const auto request = receiveDiscoveryRequest(discovery_socket);
      if (request.token == options.discovery_token) {
        if (request.type == DiscoveryRequestType::QUERY) {
          DiscoveryCoordinatorInfo info;
          info.host = options.advertise_host;
          info.tcp_port = listening.tcp_port;
          info.discovery_port = listening.discovery_port;
          info.worker_count =
              static_cast<int>(result->remote_workers_.size());
          info.min_worker_count = options.min_worker_count;
          info.closed = discovery_closed;
          sendDiscoveryCoordinatorResponse(discovery_socket, request, info);
        } else if (request.type == DiscoveryRequestType::CLOSE) {
          discovery_closed = true;
          DiscoveryCloseResult close;
          close.accepted = true;
          close.worker_count =
              static_cast<int>(result->remote_workers_.size());
          close.min_worker_count = options.min_worker_count;
          sendDiscoveryCloseAck(discovery_socket, request, close);
        }
      }
    }

    if ((descriptors[0].revents & POLLIN) != 0) {
      auto worker = acceptTcpPartitionWorker(
          &listener, std::chrono::milliseconds(1), options.compression);
      auto *worker_ptr = worker.get();
      result->remote_workers_.push_back(worker_ptr);
      result->shared_pool_->addWorker(std::move(worker));
      if (options.on_accepted) {
        options.on_accepted(worker_ptr->statusSnapshot());
      }
    }
  }
  return result;
}

RemoteWorkerPool::~RemoteWorkerPool() {
  if (!stopped_) {
    try {
      stop(/*reason=*/1, "remote worker pool closed");
    } catch (...) {
    }
  }
}

std::vector<TcpPartitionWorkerStatusSnapshot>
RemoteWorkerPool::statusSnapshots() const {
  std::vector<TcpPartitionWorkerStatusSnapshot> snapshots;
  snapshots.reserve(remote_workers_.size());
  for (const auto *worker : remote_workers_) {
    snapshots.push_back(worker->statusSnapshot());
  }
  return snapshots;
}

void RemoteWorkerPool::stop(std::uint32_t reason,
                            const std::string &message) {
  if (stopped_) {
    return;
  }
  std::exception_ptr first_error;
  for (auto *worker : remote_workers_) {
    try {
      worker->stop(reason, message);
    } catch (...) {
      if (!first_error) {
        first_error = std::current_exception();
      }
    }
  }
  stopped_ = true;
  if (first_error) {
    std::rethrow_exception(first_error);
  }
}

} // namespace mcpd4
