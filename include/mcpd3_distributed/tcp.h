#pragma once

#include <cstdint>
#include <chrono>
#include <string>
#include <vector>

namespace mcpd3_distributed {

class SocketHandle {
public:
  SocketHandle() = default;
  explicit SocketHandle(int fd);
  ~SocketHandle();

  SocketHandle(const SocketHandle &) = delete;
  SocketHandle &operator=(const SocketHandle &) = delete;

  SocketHandle(SocketHandle &&other) noexcept;
  SocketHandle &operator=(SocketHandle &&other) noexcept;

  int get() const { return fd_; }
  bool valid() const { return fd_ >= 0; }
  int release();
  void reset(int fd = -1);

private:
  int fd_ = -1;
};

SocketHandle listenTcpLoopback(std::uint16_t port, int backlog = 16);
SocketHandle listenTcp(const std::string &bind_host, std::uint16_t port,
                       int backlog = 16);
SocketHandle connectTcp(const std::string &host, std::uint16_t port);
SocketHandle acceptTcp(SocketHandle *listener,
                       std::chrono::milliseconds timeout);
std::uint16_t localPort(const SocketHandle &socket);

void sendFrameBytes(const SocketHandle &socket,
                    const std::vector<std::uint8_t> &frame);
std::vector<std::uint8_t> receiveFrameBytes(
    const SocketHandle &socket,
    std::size_t max_payload_bytes = 256ULL * 1024ULL * 1024ULL);

} // namespace mcpd3_distributed
