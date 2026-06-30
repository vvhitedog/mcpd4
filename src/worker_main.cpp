#include <mcpd4/discovery.h>
#include <mcpd4/runtime.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

std::uint16_t parsePort(const std::string &value) {
  const long parsed = std::stol(value);
  if (parsed <= 0 ||
      parsed > static_cast<long>(std::numeric_limits<std::uint16_t>::max())) {
    throw std::runtime_error("port is outside uint16 range");
  }
  return static_cast<std::uint16_t>(parsed);
}

void usage(const char *argv0) {
  std::cerr
      << "usage: " << argv0 << " HOST PORT [--name NAME]\n"
      << "       " << argv0
      << " --discover [--discovery-host HOST] [--discovery-port PORT]\n"
      << "       [--discovery-token TOKEN] [--discovery-timeout-ms N]"
         " [--name NAME]\n";
}

long parsePositiveLong(const std::string &value, const std::string &name) {
  const long parsed = std::stol(value);
  if (parsed <= 0) {
    throw std::runtime_error(name + " must be positive");
  }
  return parsed;
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 2) {
      usage(argv[0]);
      return EXIT_FAILURE;
    }

    bool discover = false;
    std::string host;
    std::uint16_t port = 0;
    std::string worker_name;
    std::string discovery_host = "255.255.255.255";
    std::uint16_t discovery_port = mcpd4::kDefaultDiscoveryPort;
    std::string discovery_token = mcpd4::kDefaultDiscoveryToken;
    long discovery_timeout_ms = 5000;

    int arg_index = 1;
    if (std::string(argv[arg_index]) == "--discover") {
      discover = true;
      ++arg_index;
    } else {
      if (argc < 3) {
        usage(argv[0]);
        return EXIT_FAILURE;
      }
      host = argv[arg_index++];
      port = parsePort(argv[arg_index++]);
    }

    for (int i = arg_index; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--name" && i + 1 < argc) {
        worker_name = argv[++i];
      } else if (arg == "--discovery-host" && i + 1 < argc) {
        discovery_host = argv[++i];
      } else if (arg == "--discovery-port" && i + 1 < argc) {
        discovery_port = parsePort(argv[++i]);
      } else if (arg == "--discovery-token" && i + 1 < argc) {
        discovery_token = argv[++i];
      } else if (arg == "--discovery-timeout-ms" && i + 1 < argc) {
        discovery_timeout_ms =
            parsePositiveLong(argv[++i], "--discovery-timeout-ms");
      } else {
        usage(argv[0]);
        return EXIT_FAILURE;
      }
    }

    if (discover) {
      const auto coordinator = mcpd4::discoverOneCoordinator(
          discovery_host, discovery_port, discovery_token,
          std::chrono::milliseconds(discovery_timeout_ms));
      host = coordinator.host;
      port = coordinator.tcp_port;
      std::cout << "discovered_coordinator host " << host << " tcp_port "
                << port << " discovery_port " << coordinator.discovery_port
                << "\n";
      std::cout.flush();
    }

    mcpd4::runWorkerClient(
        host, port, mcpd4::makeDefaultHello(worker_name));
  } catch (const std::exception &e) {
    std::cerr << "mcpd4_worker failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
