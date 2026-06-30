#pragma once

#include <mcpd4/tcp.h>

#include <chrono>
#include <cstdint>
#include <string>
#include <sys/socket.h>
#include <vector>

namespace mcpd4 {

constexpr std::uint16_t kDefaultDiscoveryPort = 50052;
constexpr const char *kDefaultDiscoveryToken = "mcpd4";

enum class DiscoveryRequestType {
  INVALID,
  QUERY,
  CLOSE
};

struct DiscoveryCoordinatorInfo {
  std::string host;
  std::uint16_t tcp_port = 0;
  std::uint16_t discovery_port = 0;
  int worker_count = 0;
  int min_worker_count = 0;
  bool closed = false;
};

struct DiscoveryCloseResult {
  bool accepted = false;
  int worker_count = 0;
  int min_worker_count = 0;
};

struct DiscoveryRequest {
  DiscoveryRequestType type = DiscoveryRequestType::INVALID;
  std::string token;
  sockaddr_storage sender_addr{};
  socklen_t sender_addr_len = 0;
};

SocketHandle bindDiscoveryUdp(const std::string &bind_host,
                              std::uint16_t port);

DiscoveryRequest receiveDiscoveryRequest(const SocketHandle &socket);

void sendDiscoveryCoordinatorResponse(
    const SocketHandle &socket, const DiscoveryRequest &request,
    const DiscoveryCoordinatorInfo &info);

void sendDiscoveryCloseAck(const SocketHandle &socket,
                           const DiscoveryRequest &request,
                           const DiscoveryCloseResult &result);

std::vector<DiscoveryCoordinatorInfo> discoverCoordinators(
    const std::string &host, std::uint16_t discovery_port,
    const std::string &token, std::chrono::milliseconds timeout);

DiscoveryCoordinatorInfo discoverOneCoordinator(
    const std::string &host, std::uint16_t discovery_port,
    const std::string &token, std::chrono::milliseconds timeout);

DiscoveryCloseResult closeCoordinatorDiscovery(
    const std::string &host, std::uint16_t discovery_port,
    const std::string &token, std::chrono::milliseconds timeout);

} // namespace mcpd4
