#include <mcpd4/tcp.h>

#include <mcpd4/protocol.h>

#include <arpa/inet.h>
#include <chrono>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#ifdef MCPD4_ENABLE_SNAPPY
#include <snappy.h>
#endif

namespace mcpd4 {
namespace {

constexpr std::uint32_t kCompressedFrameMagic = 0x345a504d; // "MPZ4"
constexpr std::uint32_t kCompressedFrameStored = 0;
constexpr std::uint32_t kCompressedFrameSnappy = 1;
constexpr std::size_t kCompressedFrameHeaderBytes = 24;
constexpr std::size_t kProtocolFrameHeaderBytes = 12;

std::runtime_error socketError(const std::string &message) {
  return std::runtime_error(message + ": " + std::strerror(errno));
}

void writeAll(int fd, const std::uint8_t *data, std::size_t size) {
  std::size_t written = 0;
  while (written < size) {
    const auto n =
        ::send(fd, data + written, size - written, MSG_NOSIGNAL);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw socketError("socket write failed");
    }
    if (n == 0) {
      throw std::runtime_error("socket closed during write");
    }
    written += static_cast<std::size_t>(n);
  }
}

void readAll(int fd, std::uint8_t *data, std::size_t size) {
  std::size_t read = 0;
  while (read < size) {
    const auto n = ::recv(fd, data + read, size - read, 0);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw socketError("socket read failed");
    }
    if (n == 0) {
      throw std::runtime_error("socket closed during read");
    }
    read += static_cast<std::size_t>(n);
  }
}

std::uint64_t readLittleU64(const std::uint8_t *data) {
  std::uint64_t value = 0;
  for (int shift = 0; shift < 64; shift += 8) {
    value |= static_cast<std::uint64_t>(*data++) << shift;
  }
  return value;
}

std::uint32_t readLittleU32(const std::uint8_t *data) {
  std::uint32_t value = 0;
  for (int shift = 0; shift < 32; shift += 8) {
    value |= static_cast<std::uint32_t>(*data++) << shift;
  }
  return value;
}

void appendLittleU32(std::vector<std::uint8_t> *bytes,
                     std::uint32_t value) {
  for (int shift = 0; shift < 32; shift += 8) {
    bytes->push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
  }
}

void appendLittleU64(std::vector<std::uint8_t> *bytes,
                     std::uint64_t value) {
  for (int shift = 0; shift < 64; shift += 8) {
    bytes->push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
  }
}

std::uint64_t elapsedUs(std::chrono::steady_clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(
          std::chrono::steady_clock::now() - start)
          .count());
}

void requireFrameWithinLimit(std::uint64_t size,
                             std::size_t max_frame_bytes,
                             const char *message) {
  if (size > static_cast<std::uint64_t>(max_frame_bytes)) {
    throw std::runtime_error(message);
  }
}

void enableTcpNoDelay(const SocketHandle &socket) {
  int one = 1;
  if (::setsockopt(socket.get(), IPPROTO_TCP, TCP_NODELAY, &one,
                   sizeof(one)) != 0) {
    throw socketError("failed to enable TCP_NODELAY");
  }
}

} // namespace

SocketHandle::SocketHandle(int fd) : fd_(fd) {}

SocketHandle::~SocketHandle() { reset(); }

SocketHandle::SocketHandle(SocketHandle &&other) noexcept
    : fd_(other.release()) {}

SocketHandle &SocketHandle::operator=(SocketHandle &&other) noexcept {
  if (this != &other) {
    reset(other.release());
  }
  return *this;
}

int SocketHandle::release() {
  const int fd = fd_;
  fd_ = -1;
  return fd;
}

void SocketHandle::reset(int fd) {
  if (fd_ >= 0) {
    ::close(fd_);
  }
  fd_ = fd;
}

SocketHandle listenTcpLoopback(std::uint16_t port, int backlog) {
  return listenTcp("127.0.0.1", port, backlog);
}

SocketHandle listenTcp(const std::string &bind_host, std::uint16_t port,
                       int backlog) {
  SocketHandle socket(::socket(AF_INET, SOCK_STREAM, 0));
  if (!socket.valid()) {
    throw socketError("failed to create listen socket");
  }

  int one = 1;
  if (::setsockopt(socket.get(), SOL_SOCKET, SO_REUSEADDR, &one,
                   sizeof(one)) != 0) {
    throw socketError("failed to set SO_REUSEADDR");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  if (::inet_pton(AF_INET, bind_host.c_str(), &addr.sin_addr) != 1) {
    throw std::runtime_error("listen bind host must be an IPv4 address");
  }
  addr.sin_port = htons(port);
  if (::bind(socket.get(), reinterpret_cast<sockaddr *>(&addr),
             sizeof(addr)) != 0) {
    throw socketError("failed to bind listen socket");
  }
  if (::listen(socket.get(), backlog) != 0) {
    throw socketError("failed to listen");
  }
  return socket;
}

SocketHandle connectTcp(const std::string &host, std::uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;

  addrinfo *infos = nullptr;
  const std::string port_string = std::to_string(port);
  const int rc = ::getaddrinfo(host.c_str(), port_string.c_str(), &hints,
                               &infos);
  if (rc != 0) {
    throw std::runtime_error("failed to resolve host: " +
                             std::string(::gai_strerror(rc)));
  }

  SocketHandle socket;
  for (addrinfo *info = infos; info != nullptr; info = info->ai_next) {
    SocketHandle candidate(::socket(info->ai_family, info->ai_socktype,
                                    info->ai_protocol));
    if (!candidate.valid()) {
      continue;
    }
    if (::connect(candidate.get(), info->ai_addr, info->ai_addrlen) == 0) {
      enableTcpNoDelay(candidate);
      socket = std::move(candidate);
      break;
    }
  }
  ::freeaddrinfo(infos);
  if (!socket.valid()) {
    throw socketError("failed to connect");
  }
  return socket;
}

SocketHandle acceptTcp(SocketHandle *listener,
                       std::chrono::milliseconds timeout) {
  pollfd pfd{};
  pfd.fd = listener->get();
  pfd.events = POLLIN;
  const int rc = ::poll(&pfd, 1, static_cast<int>(timeout.count()));
  if (rc < 0) {
    if (errno == EINTR) {
      throw std::runtime_error("accept interrupted");
    }
    throw socketError("poll failed while accepting");
  }
  if (rc == 0) {
    throw std::runtime_error("timed out waiting for worker connection");
  }

  int fd = -1;
  do {
    fd = ::accept(listener->get(), nullptr, nullptr);
  } while (fd < 0 && errno == EINTR);
  if (fd < 0) {
    throw socketError("accept failed");
  }
  SocketHandle socket(fd);
  enableTcpNoDelay(socket);
  return socket;
}

std::uint16_t localPort(const SocketHandle &socket) {
  sockaddr_in addr{};
  socklen_t len = sizeof(addr);
  if (::getsockname(socket.get(), reinterpret_cast<sockaddr *>(&addr), &len) !=
      0) {
    throw socketError("getsockname failed");
  }
  return ntohs(addr.sin_port);
}

bool tcpNoDelayEnabled(const SocketHandle &socket) {
  int enabled = 0;
  socklen_t length = sizeof(enabled);
  if (::getsockopt(socket.get(), IPPROTO_TCP, TCP_NODELAY, &enabled,
                   &length) != 0) {
    throw socketError("failed to read TCP_NODELAY");
  }
  return enabled != 0;
}

bool snappyCompressionAvailable() {
#ifdef MCPD4_ENABLE_SNAPPY
  return true;
#else
  return false;
#endif
}

const char *transportCompressionName(TransportCompression compression) {
  switch (compression) {
  case TransportCompression::NONE:
    return "none";
  case TransportCompression::SNAPPY:
    return "snappy";
  }
  return "unknown";
}

TransportCompression parseTransportCompression(const std::string &value) {
  if (value == "none") {
    return TransportCompression::NONE;
  }
  if (value == "snappy") {
    return TransportCompression::SNAPPY;
  }
  throw std::runtime_error("unknown RPC compression mode: " + value);
}

void sendFrameBytes(const SocketHandle &socket,
                    const std::vector<std::uint8_t> &frame,
                    TransportCompression compression,
                    FrameTransferStats *stats,
                    std::size_t max_frame_bytes) {
  if (!socket.valid()) {
    throw std::runtime_error("cannot write to invalid socket");
  }
  requireFrameWithinLimit(frame.size(), max_frame_bytes,
                          "frame payload exceeds maximum size");
  if (stats != nullptr) {
    *stats = {};
    stats->logical_bytes = static_cast<std::uint64_t>(frame.size());
  }
  if (compression == TransportCompression::NONE) {
    writeAll(socket.get(), frame.data(), frame.size());
    if (stats != nullptr) {
      stats->wire_bytes = static_cast<std::uint64_t>(frame.size());
    }
    return;
  }
  if (compression != TransportCompression::SNAPPY) {
    throw std::runtime_error("unknown transport compression mode");
  }
  if (stats != nullptr) {
    stats->compression_requested = true;
  }

#ifndef MCPD4_ENABLE_SNAPPY
  throw std::runtime_error(
      "snappy compression requested but this binary was built without "
      "MCPD4_ENABLE_SNAPPY");
#else
  const auto start = std::chrono::steady_clock::now();
  const std::size_t max_compressed_size =
      snappy::MaxCompressedLength(frame.size());
  std::vector<std::uint8_t> compressed(max_compressed_size);
  std::size_t compressed_size = max_compressed_size;
  snappy::RawCompress(
      reinterpret_cast<const char *>(frame.data()), frame.size(),
      reinterpret_cast<char *>(compressed.data()), &compressed_size);
  const auto compression_us = elapsedUs(start);
  compressed.resize(compressed_size);

  const bool use_compressed =
      compressed.size() + kCompressedFrameHeaderBytes < frame.size();
  const auto &payload = use_compressed ? compressed : frame;
  std::vector<std::uint8_t> envelope;
  envelope.reserve(kCompressedFrameHeaderBytes + payload.size());
  appendLittleU32(&envelope, kCompressedFrameMagic);
  appendLittleU32(&envelope, use_compressed ? kCompressedFrameSnappy
                                            : kCompressedFrameStored);
  appendLittleU64(&envelope, frame.size());
  appendLittleU64(&envelope, payload.size());
  envelope.insert(envelope.end(), payload.begin(), payload.end());
  writeAll(socket.get(), envelope.data(), envelope.size());
  if (stats != nullptr) {
    stats->wire_bytes = static_cast<std::uint64_t>(envelope.size());
    stats->compression_wall_us = compression_us;
    stats->compression_requested = true;
    stats->compressed = use_compressed;
  }
#endif
}

std::vector<std::uint8_t> receiveFrameBytes(
    const SocketHandle &socket, std::size_t max_frame_bytes,
    TransportCompression compression, FrameTransferStats *stats) {
  if (stats != nullptr) {
    *stats = {};
  }
  if (compression == TransportCompression::SNAPPY) {
    if (stats != nullptr) {
      stats->compression_requested = true;
    }
    std::vector<std::uint8_t> header(kCompressedFrameHeaderBytes);
    readAll(socket.get(), header.data(), header.size());
    if (readLittleU32(header.data()) != kCompressedFrameMagic) {
      throw std::runtime_error("compressed transport frame has bad magic");
    }
    const auto codec = readLittleU32(header.data() + 4);
    const auto logical_size = readLittleU64(header.data() + 8);
    const auto payload_size = readLittleU64(header.data() + 16);
    requireFrameWithinLimit(logical_size, max_frame_bytes,
                            "frame payload exceeds maximum size");
    requireFrameWithinLimit(payload_size, max_frame_bytes,
                            "transport frame exceeds maximum size");
    std::vector<std::uint8_t> payload(static_cast<std::size_t>(payload_size));
    readAll(socket.get(), payload.data(), payload.size());

    std::vector<std::uint8_t> frame;
    std::uint64_t decompression_us = 0;
    bool was_compressed = false;
    if (codec == kCompressedFrameStored) {
      if (payload_size != logical_size) {
        throw std::runtime_error("stored transport frame size mismatch");
      }
      frame = std::move(payload);
    } else if (codec == kCompressedFrameSnappy) {
      was_compressed = true;
#ifndef MCPD4_ENABLE_SNAPPY
      throw std::runtime_error(
          "snappy frame received but this binary was built without "
          "MCPD4_ENABLE_SNAPPY");
#else
      std::size_t snappy_uncompressed_size = 0;
      if (!snappy::GetUncompressedLength(
              reinterpret_cast<const char *>(payload.data()), payload.size(),
              &snappy_uncompressed_size)) {
        throw std::runtime_error("failed to read snappy uncompressed size");
      }
      if (snappy_uncompressed_size != logical_size) {
        throw std::runtime_error("snappy uncompressed size mismatch");
      }
      frame.resize(static_cast<std::size_t>(logical_size));
      const auto start = std::chrono::steady_clock::now();
      const bool ok = snappy::RawUncompress(
          reinterpret_cast<const char *>(payload.data()), payload.size(),
          reinterpret_cast<char *>(frame.data()));
      decompression_us = elapsedUs(start);
      if (!ok) {
        throw std::runtime_error("snappy decompression failed");
      }
#endif
    } else {
      throw std::runtime_error("unknown compressed transport codec");
    }
    (void)decodeFrame(frame);
    if (stats != nullptr) {
      stats->logical_bytes = logical_size;
      stats->wire_bytes =
          static_cast<std::uint64_t>(kCompressedFrameHeaderBytes) +
          payload_size;
      stats->decompression_wall_us = decompression_us;
      stats->compression_requested = true;
      stats->compressed = was_compressed;
    }
    return frame;
  }
  if (compression != TransportCompression::NONE) {
    throw std::runtime_error("unknown transport compression mode");
  }
  std::vector<std::uint8_t> frame(12);
  readAll(socket.get(), frame.data(), frame.size());

  const auto payload_size = readLittleU64(frame.data() + 4);
  if (max_frame_bytes < kProtocolFrameHeaderBytes ||
      payload_size >
          static_cast<std::uint64_t>(max_frame_bytes -
                                     kProtocolFrameHeaderBytes)) {
    throw std::runtime_error("frame payload exceeds maximum size");
  }
  frame.resize(frame.size() + static_cast<std::size_t>(payload_size));
  readAll(socket.get(), frame.data() + 12,
          static_cast<std::size_t>(payload_size));
  (void)decodeFrame(frame);
  if (stats != nullptr) {
    stats->logical_bytes = static_cast<std::uint64_t>(frame.size());
    stats->wire_bytes = static_cast<std::uint64_t>(frame.size());
  }
  return frame;
}

} // namespace mcpd4
