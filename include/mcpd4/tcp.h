#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mcpd4 {

enum class TransportCompression {
  NONE,
  SNAPPY,
};

constexpr std::size_t kDefaultMaxFrameBytes = 1024ULL * 1024ULL * 1024ULL;

struct FrameTransferStats {
  std::uint64_t logical_bytes = 0;
  std::uint64_t wire_bytes = 0;
  std::uint64_t compression_wall_us = 0;
  std::uint64_t decompression_wall_us = 0;
  bool compression_requested = false;
  bool compressed = false;
};

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
bool tcpNoDelayEnabled(const SocketHandle &socket);

bool snappyCompressionAvailable();
const char *transportCompressionName(TransportCompression compression);
TransportCompression parseTransportCompression(const std::string &value);

void sendFrameBytes(
    const SocketHandle &socket, const std::vector<std::uint8_t> &frame,
    TransportCompression compression = TransportCompression::NONE,
    FrameTransferStats *stats = nullptr,
    std::size_t max_frame_bytes = kDefaultMaxFrameBytes);
std::vector<std::uint8_t> receiveFrameBytes(
    const SocketHandle &socket,
    std::size_t max_frame_bytes = kDefaultMaxFrameBytes,
    TransportCompression compression = TransportCompression::NONE,
    FrameTransferStats *stats = nullptr);

} // namespace mcpd4
