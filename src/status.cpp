#include <mcpd4/status.h>

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <netdb.h>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>
#include <unordered_map>
#include <utility>
#include <unistd.h>

namespace mcpd4 {
namespace {

constexpr const char *kPrefix = "MCPD4_STATUS_V1";

std::runtime_error socketError(const std::string &message) {
  return std::runtime_error(message + ": " + std::strerror(errno));
}

void validateToken(const std::string &token) {
  if (token.empty()) {
    throw std::runtime_error("status token must not be empty");
  }
  for (const char ch : token) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      throw std::runtime_error("status token must not contain whitespace");
    }
  }
}

SocketHandle createUdpSocket() {
  SocketHandle socket(::socket(AF_INET, SOCK_DGRAM, 0));
  if (!socket.valid()) {
    throw socketError("failed to create status UDP socket");
  }
  return socket;
}

SocketHandle bindStatusUdp(const std::string &bind_host, std::uint16_t port) {
  auto socket = createUdpSocket();
  int one = 1;
  if (::setsockopt(socket.get(), SOL_SOCKET, SO_REUSEADDR, &one,
                   sizeof(one)) != 0) {
    throw socketError("failed to set status UDP SO_REUSEADDR");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  if (::inet_pton(AF_INET, bind_host.c_str(), &addr.sin_addr) != 1) {
    throw std::runtime_error("status bind host must be an IPv4 address");
  }
  addr.sin_port = htons(port);
  if (::bind(socket.get(), reinterpret_cast<sockaddr *>(&addr),
             sizeof(addr)) != 0) {
    throw socketError("failed to bind status UDP socket");
  }
  return socket;
}

std::unordered_map<std::string, std::string>
parseFields(std::istringstream *stream) {
  std::unordered_map<std::string, std::string> fields;
  std::string key;
  std::string value;
  while (*stream >> key >> value) {
    fields[key] = value;
  }
  return fields;
}

std::string queryMessage(const std::string &token) {
  validateToken(token);
  return std::string(kPrefix) + " QUERY token " + token;
}

std::string responseMessage(const std::string &token,
                            const std::string &snapshot) {
  validateToken(token);
  return std::string(kPrefix) + " STATUS token " + token + " " + snapshot;
}

sockaddr_in resolveUdpDestination(const std::string &host,
                                  std::uint16_t port) {
  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_DGRAM;

  addrinfo *infos = nullptr;
  const std::string port_string = std::to_string(port);
  const int rc =
      ::getaddrinfo(host.c_str(), port_string.c_str(), &hints, &infos);
  if (rc != 0) {
    throw std::runtime_error("failed to resolve status host: " +
                             std::string(::gai_strerror(rc)));
  }
  if (infos == nullptr) {
    throw std::runtime_error("status host resolved to no addresses");
  }
  sockaddr_in addr = *reinterpret_cast<sockaddr_in *>(infos->ai_addr);
  ::freeaddrinfo(infos);
  return addr;
}

void sendUdp(const SocketHandle &socket, const std::string &message,
             const sockaddr *addr, socklen_t addr_len) {
  const auto rc =
      ::sendto(socket.get(), message.data(), message.size(), 0, addr, addr_len);
  if (rc < 0) {
    throw socketError("failed to send status datagram");
  }
}

} // namespace

StatusServer::StatusServer(const std::string &bind_host, std::uint16_t port,
                           std::string token,
                           SnapshotCallback snapshot_callback)
    : socket_(bindStatusUdp(bind_host, port)), token_(std::move(token)),
      snapshot_callback_(std::move(snapshot_callback)) {
  validateToken(token_);
  if (!snapshot_callback_) {
    throw std::runtime_error("status snapshot callback must be set");
  }
  thread_ = std::thread([this] { run(); });
}

StatusServer::~StatusServer() { stop(); }

std::uint16_t StatusServer::port() const { return localPort(socket_); }

void StatusServer::stop() {
  stopped_.store(true);
  if (thread_.joinable()) {
    thread_.join();
  }
  socket_.reset();
}

void StatusServer::run() {
  while (!stopped_.load()) {
    pollfd pfd{};
    pfd.fd = socket_.get();
    pfd.events = POLLIN;
    const int rc = ::poll(&pfd, 1, 100);
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    if (rc == 0 || (pfd.revents & POLLIN) == 0) {
      continue;
    }

    char buffer[2048] = {};
    sockaddr_storage sender{};
    socklen_t sender_len = sizeof(sender);
    const auto n =
        ::recvfrom(socket_.get(), buffer, sizeof(buffer) - 1, 0,
                   reinterpret_cast<sockaddr *>(&sender), &sender_len);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      return;
    }
    buffer[n] = '\0';

    std::istringstream in(std::string(buffer, static_cast<std::size_t>(n)));
    std::string prefix;
    std::string kind;
    in >> prefix >> kind;
    if (prefix != kPrefix || kind != "QUERY") {
      continue;
    }
    const auto fields = parseFields(&in);
    const auto token_iter = fields.find("token");
    if (token_iter == fields.end() || token_iter->second != token_) {
      continue;
    }

    const std::string response =
        responseMessage(token_, snapshot_callback_());
    sendUdp(socket_, response, reinterpret_cast<const sockaddr *>(&sender),
            sender_len);
  }
}

std::string queryStatus(const std::string &host, std::uint16_t port,
                        const std::string &token,
                        std::chrono::milliseconds timeout) {
  auto socket = createUdpSocket();
  const sockaddr_in addr = resolveUdpDestination(host, port);
  sendUdp(socket, queryMessage(token), reinterpret_cast<const sockaddr *>(&addr),
          sizeof(addr));

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  while (std::chrono::steady_clock::now() < deadline) {
    const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now());
    pollfd pfd{};
    pfd.fd = socket.get();
    pfd.events = POLLIN;
    const int rc =
        ::poll(&pfd, 1, std::max(0, static_cast<int>(remaining.count())));
    if (rc < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw socketError("poll failed while querying status");
    }
    if (rc == 0) {
      break;
    }

    char buffer[8192] = {};
    sockaddr_storage sender{};
    socklen_t sender_len = sizeof(sender);
    const auto n =
        ::recvfrom(socket.get(), buffer, sizeof(buffer) - 1, 0,
                   reinterpret_cast<sockaddr *>(&sender), &sender_len);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw socketError("failed to receive status response");
    }
    buffer[n] = '\0';

    std::istringstream in(std::string(buffer, static_cast<std::size_t>(n)));
    std::string prefix;
    std::string kind;
    in >> prefix >> kind;
    if (prefix != kPrefix || kind != "STATUS") {
      continue;
    }
    const auto fields = parseFields(&in);
    const auto token_iter = fields.find("token");
    if (token_iter == fields.end() || token_iter->second != token) {
      continue;
    }
    const auto token_pos = std::string(buffer).find(" token " + token + " ");
    if (token_pos == std::string::npos) {
      continue;
    }
    return std::string(buffer).substr(token_pos + token.size() + 8);
  }
  throw std::runtime_error("timed out waiting for status response");
}

} // namespace mcpd4
