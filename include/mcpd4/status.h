#pragma once

#include <mcpd4/tcp.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>

namespace mcpd4 {

constexpr std::uint16_t kDefaultStatusPort = 50053;
constexpr const char *kDefaultStatusToken = "mcpd4";

class StatusServer {
public:
  using SnapshotCallback = std::function<std::string()>;

  StatusServer(const std::string &bind_host, std::uint16_t port,
               std::string token, SnapshotCallback snapshot_callback);
  ~StatusServer();

  StatusServer(const StatusServer &) = delete;
  StatusServer &operator=(const StatusServer &) = delete;

  std::uint16_t port() const;
  void stop();

private:
  void run();

  SocketHandle socket_;
  std::string token_;
  SnapshotCallback snapshot_callback_;
  std::atomic<bool> stopped_{false};
  std::thread thread_;
};

std::string queryStatus(const std::string &host, std::uint16_t port,
                        const std::string &token,
                        std::chrono::milliseconds timeout);

} // namespace mcpd4
