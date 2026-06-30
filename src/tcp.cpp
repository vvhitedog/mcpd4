#include <mcpd4/tcp.h>

#include <mcpd4/protocol.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

namespace mcpd4 {
namespace {

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
  return SocketHandle(fd);
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

void sendFrameBytes(const SocketHandle &socket,
                    const std::vector<std::uint8_t> &frame) {
  if (!socket.valid()) {
    throw std::runtime_error("cannot write to invalid socket");
  }
  writeAll(socket.get(), frame.data(), frame.size());
}

std::vector<std::uint8_t> receiveFrameBytes(
    const SocketHandle &socket, std::size_t max_payload_bytes) {
  std::vector<std::uint8_t> frame(12);
  readAll(socket.get(), frame.data(), frame.size());

  const auto payload_size = readLittleU64(frame.data() + 4);
  if (payload_size > max_payload_bytes) {
    throw std::runtime_error("frame payload exceeds maximum size");
  }
  frame.resize(frame.size() + static_cast<std::size_t>(payload_size));
  readAll(socket.get(), frame.data() + 12,
          static_cast<std::size_t>(payload_size));
  (void)decodeFrame(frame);
  return frame;
}

} // namespace mcpd4
