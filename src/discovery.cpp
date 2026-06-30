#include <mcpd4/discovery.h>

#include <arpa/inet.h>
#include <algorithm>
#include <cerrno>
#include <cctype>
#include <cstring>
#include <limits>
#include <netdb.h>
#include <poll.h>
#include <sstream>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/types.h>
#include <unordered_map>
#include <unistd.h>

namespace mcpd4 {
namespace {

constexpr const char *kPrefix = "MCPD4_DISCOVERY_V1";

std::runtime_error socketError(const std::string &message) {
  return std::runtime_error(message + ": " + std::strerror(errno));
}

void validateToken(const std::string &token) {
  if (token.empty()) {
    throw std::runtime_error("discovery token must not be empty");
  }
  for (const char ch : token) {
    if (std::isspace(static_cast<unsigned char>(ch))) {
      throw std::runtime_error("discovery token must not contain whitespace");
    }
  }
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

bool parseBoolField(const std::unordered_map<std::string, std::string> &fields,
                    const std::string &key) {
  const auto iter = fields.find(key);
  if (iter == fields.end()) {
    return false;
  }
  return iter->second == "1" || iter->second == "true";
}

int parseIntField(const std::unordered_map<std::string, std::string> &fields,
                  const std::string &key) {
  const auto iter = fields.find(key);
  if (iter == fields.end()) {
    return 0;
  }
  return std::stoi(iter->second);
}

std::uint16_t parsePortField(
    const std::unordered_map<std::string, std::string> &fields,
    const std::string &key) {
  const int parsed = parseIntField(fields, key);
  if (parsed < 0 ||
      parsed > static_cast<int>(std::numeric_limits<std::uint16_t>::max())) {
    throw std::runtime_error("discovery port field is outside uint16 range");
  }
  return static_cast<std::uint16_t>(parsed);
}

std::string senderHost(const sockaddr_storage &addr) {
  char buffer[INET_ADDRSTRLEN] = {};
  if (addr.ss_family != AF_INET) {
    return {};
  }
  const auto *ipv4 = reinterpret_cast<const sockaddr_in *>(&addr);
  if (::inet_ntop(AF_INET, &ipv4->sin_addr, buffer, sizeof(buffer)) ==
      nullptr) {
    return {};
  }
  return buffer;
}

SocketHandle createUdpSocket() {
  SocketHandle socket(::socket(AF_INET, SOCK_DGRAM, 0));
  if (!socket.valid()) {
    throw socketError("failed to create UDP socket");
  }
  return socket;
}

void sendUdp(const SocketHandle &socket, const std::string &message,
             const sockaddr *addr, socklen_t addr_len) {
  const auto rc =
      ::sendto(socket.get(), message.data(), message.size(), 0, addr, addr_len);
  if (rc < 0) {
    throw socketError("failed to send discovery datagram");
  }
}

std::string queryMessage(const std::string &token) {
  validateToken(token);
  return std::string(kPrefix) + " QUERY token " + token;
}

std::string closeMessage(const std::string &token) {
  validateToken(token);
  return std::string(kPrefix) + " CLOSE token " + token;
}

std::string responseMessage(const DiscoveryCoordinatorInfo &info,
                            const std::string &token) {
  validateToken(token);
  const std::string host = info.host.empty() ? "-" : info.host;
  std::ostringstream out;
  out << kPrefix << " COORDINATOR token " << token << " host " << host
      << " tcp_port " << info.tcp_port << " discovery_port "
      << info.discovery_port << " worker_count " << info.worker_count
      << " min_worker_count " << info.min_worker_count << " closed "
      << (info.closed ? 1 : 0);
  return out.str();
}

std::string closeAckMessage(const DiscoveryCloseResult &result,
                            const std::string &token) {
  validateToken(token);
  std::ostringstream out;
  out << kPrefix << " CLOSE_ACK token " << token << " accepted "
      << (result.accepted ? 1 : 0) << " worker_count " << result.worker_count
      << " min_worker_count " << result.min_worker_count;
  return out.str();
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
    throw std::runtime_error("failed to resolve discovery host: " +
                             std::string(::gai_strerror(rc)));
  }
  if (infos == nullptr) {
    throw std::runtime_error("discovery host resolved to no addresses");
  }
  sockaddr_in addr =
      *reinterpret_cast<sockaddr_in *>(infos->ai_addr);
  ::freeaddrinfo(infos);
  return addr;
}

void sendDiscoveryMessage(const SocketHandle &socket, const std::string &host,
                          std::uint16_t port,
                          const std::string &message) {
  if (host == "255.255.255.255") {
    int one = 1;
    if (::setsockopt(socket.get(), SOL_SOCKET, SO_BROADCAST, &one,
                     sizeof(one)) != 0) {
      throw socketError("failed to enable UDP broadcast");
    }
  }

  const sockaddr_in addr = resolveUdpDestination(host, port);
  sendUdp(socket, message, reinterpret_cast<const sockaddr *>(&addr),
          sizeof(addr));
}

} // namespace

SocketHandle bindDiscoveryUdp(const std::string &bind_host,
                              std::uint16_t port) {
  auto socket = createUdpSocket();
  int one = 1;
  if (::setsockopt(socket.get(), SOL_SOCKET, SO_REUSEADDR, &one,
                   sizeof(one)) != 0) {
    throw socketError("failed to set UDP SO_REUSEADDR");
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  if (::inet_pton(AF_INET, bind_host.c_str(), &addr.sin_addr) != 1) {
    throw std::runtime_error("discovery bind host must be an IPv4 address");
  }
  addr.sin_port = htons(port);
  if (::bind(socket.get(), reinterpret_cast<sockaddr *>(&addr),
             sizeof(addr)) != 0) {
    throw socketError("failed to bind discovery UDP socket");
  }
  return socket;
}

DiscoveryRequest receiveDiscoveryRequest(const SocketHandle &socket) {
  char buffer[2048] = {};
  DiscoveryRequest request;
  request.sender_addr_len = sizeof(request.sender_addr);
  const auto n = ::recvfrom(
      socket.get(), buffer, sizeof(buffer) - 1, 0,
      reinterpret_cast<sockaddr *>(&request.sender_addr),
      &request.sender_addr_len);
  if (n < 0) {
    throw socketError("failed to receive discovery datagram");
  }
  buffer[n] = '\0';

  std::istringstream in(std::string(buffer, static_cast<std::size_t>(n)));
  std::string prefix;
  std::string kind;
  in >> prefix >> kind;
  if (prefix != kPrefix) {
    return request;
  }
  const auto fields = parseFields(&in);
  const auto token_iter = fields.find("token");
  if (token_iter != fields.end()) {
    request.token = token_iter->second;
  }
  if (kind == "QUERY") {
    request.type = DiscoveryRequestType::QUERY;
  } else if (kind == "CLOSE") {
    request.type = DiscoveryRequestType::CLOSE;
  }
  return request;
}

void sendDiscoveryCoordinatorResponse(
    const SocketHandle &socket, const DiscoveryRequest &request,
    const DiscoveryCoordinatorInfo &info) {
  sendUdp(socket, responseMessage(info, request.token),
          reinterpret_cast<const sockaddr *>(&request.sender_addr),
          request.sender_addr_len);
}

void sendDiscoveryCloseAck(const SocketHandle &socket,
                           const DiscoveryRequest &request,
                           const DiscoveryCloseResult &result) {
  sendUdp(socket, closeAckMessage(result, request.token),
          reinterpret_cast<const sockaddr *>(&request.sender_addr),
          request.sender_addr_len);
}

std::vector<DiscoveryCoordinatorInfo> discoverCoordinators(
    const std::string &host, std::uint16_t discovery_port,
    const std::string &token, std::chrono::milliseconds timeout) {
  auto socket = createUdpSocket();
  sendDiscoveryMessage(socket, host, discovery_port, queryMessage(token));

  const auto deadline = std::chrono::steady_clock::now() + timeout;
  std::vector<DiscoveryCoordinatorInfo> coordinators;
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
      throw socketError("poll failed while discovering coordinators");
    }
    if (rc == 0) {
      break;
    }

    char buffer[2048] = {};
    sockaddr_storage sender{};
    socklen_t sender_len = sizeof(sender);
    const auto n =
        ::recvfrom(socket.get(), buffer, sizeof(buffer) - 1, 0,
                   reinterpret_cast<sockaddr *>(&sender), &sender_len);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw socketError("failed to receive discovery response");
    }
    buffer[n] = '\0';
    std::istringstream in(std::string(buffer, static_cast<std::size_t>(n)));
    std::string prefix;
    std::string kind;
    in >> prefix >> kind;
    if (prefix != kPrefix || kind != "COORDINATOR") {
      continue;
    }
    const auto fields = parseFields(&in);
    const auto token_iter = fields.find("token");
    if (token_iter == fields.end() || token_iter->second != token) {
      continue;
    }

    DiscoveryCoordinatorInfo info;
    const auto host_iter = fields.find("host");
    if (host_iter != fields.end() && host_iter->second != "-") {
      info.host = host_iter->second;
    } else {
      info.host = senderHost(sender);
    }
    info.tcp_port = parsePortField(fields, "tcp_port");
    info.discovery_port = parsePortField(fields, "discovery_port");
    info.worker_count = parseIntField(fields, "worker_count");
    info.min_worker_count = parseIntField(fields, "min_worker_count");
    info.closed = parseBoolField(fields, "closed");
    coordinators.push_back(std::move(info));
  }
  return coordinators;
}

DiscoveryCoordinatorInfo discoverOneCoordinator(
    const std::string &host, std::uint16_t discovery_port,
    const std::string &token, std::chrono::milliseconds timeout) {
  const auto coordinators =
      discoverCoordinators(host, discovery_port, token, timeout);
  if (coordinators.empty()) {
    throw std::runtime_error("no mcpd4 coordinator discovered");
  }
  if (coordinators.size() > 1) {
    throw std::runtime_error(
        "multiple mcpd4 coordinators discovered; use an explicit host");
  }
  return coordinators.front();
}

DiscoveryCloseResult closeCoordinatorDiscovery(
    const std::string &host, std::uint16_t discovery_port,
    const std::string &token, std::chrono::milliseconds timeout) {
  auto socket = createUdpSocket();
  sendDiscoveryMessage(socket, host, discovery_port, closeMessage(token));

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
      throw socketError("poll failed while closing discovery");
    }
    if (rc == 0) {
      break;
    }

    char buffer[2048] = {};
    sockaddr_storage sender{};
    socklen_t sender_len = sizeof(sender);
    const auto n =
        ::recvfrom(socket.get(), buffer, sizeof(buffer) - 1, 0,
                   reinterpret_cast<sockaddr *>(&sender), &sender_len);
    if (n < 0) {
      if (errno == EINTR) {
        continue;
      }
      throw socketError("failed to receive discovery close response");
    }
    buffer[n] = '\0';

    std::istringstream in(std::string(buffer, static_cast<std::size_t>(n)));
    std::string prefix;
    std::string kind;
    in >> prefix >> kind;
    if (prefix != kPrefix || kind != "CLOSE_ACK") {
      continue;
    }
    const auto fields = parseFields(&in);
    const auto token_iter = fields.find("token");
    if (token_iter == fields.end() || token_iter->second != token) {
      continue;
    }

    DiscoveryCloseResult result;
    result.accepted = parseBoolField(fields, "accepted");
    result.worker_count = parseIntField(fields, "worker_count");
    result.min_worker_count = parseIntField(fields, "min_worker_count");
    return result;
  }
  throw std::runtime_error("timed out waiting for discovery close response");
}

} // namespace mcpd4
