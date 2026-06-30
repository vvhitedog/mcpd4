#include <mcpd4/discovery.h>

#include <chrono>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {

std::uint16_t parsePort(const std::string &value, const std::string &name) {
  const long parsed = std::stol(value);
  if (parsed <= 0 ||
      parsed > static_cast<long>(std::numeric_limits<std::uint16_t>::max())) {
    throw std::runtime_error(name + " is outside uint16 range");
  }
  return static_cast<std::uint16_t>(parsed);
}

long parsePositiveLong(const std::string &value, const std::string &name) {
  const long parsed = std::stol(value);
  if (parsed <= 0) {
    throw std::runtime_error(name + " must be positive");
  }
  return parsed;
}

void usage(const char *argv0) {
  std::cerr
      << "usage: " << argv0
      << " list [--host HOST] [--port PORT] [--token TOKEN]"
         " [--timeout-ms N]\n"
      << "       " << argv0
      << " close --host HOST [--port PORT] [--token TOKEN]"
         " [--timeout-ms N]\n";
}

struct Config {
  std::string command;
  std::string host = "255.255.255.255";
  std::uint16_t port = mcpd4::kDefaultDiscoveryPort;
  std::string token = mcpd4::kDefaultDiscoveryToken;
  long timeout_ms = 5000;
};

Config parseArgs(int argc, char **argv) {
  if (argc < 2) {
    throw std::runtime_error("command is required");
  }
  Config config;
  config.command = argv[1];
  if (config.command != "list" && config.command != "close") {
    throw std::runtime_error("command must be list or close");
  }
  if (config.command == "close") {
    config.host.clear();
  }

  for (int i = 2; i < argc; ++i) {
    const std::string arg = argv[i];
    auto require_value = [&](const std::string &name) -> std::string {
      if (i + 1 >= argc) {
        throw std::runtime_error(name + " requires a value");
      }
      return argv[++i];
    };

    if (arg == "--host") {
      config.host = require_value(arg);
    } else if (arg == "--port") {
      config.port = parsePort(require_value(arg), arg);
    } else if (arg == "--token") {
      config.token = require_value(arg);
    } else if (arg == "--timeout-ms") {
      config.timeout_ms = parsePositiveLong(require_value(arg), arg);
    } else {
      throw std::runtime_error("unknown argument: " + arg);
    }
  }

  if (config.host.empty()) {
    throw std::runtime_error("--host is required for close");
  }
  return config;
}

} // namespace

int main(int argc, char **argv) {
  try {
    const Config config = parseArgs(argc, argv);
    const auto timeout = std::chrono::milliseconds(config.timeout_ms);
    if (config.command == "list") {
      const auto coordinators = mcpd4::discoverCoordinators(
          config.host, config.port, config.token, timeout);
      if (coordinators.empty()) {
        std::cout << "no_coordinators\n";
        return EXIT_FAILURE;
      }
      for (const auto &coordinator : coordinators) {
        std::cout << "coordinator host " << coordinator.host << " tcp_port "
                  << coordinator.tcp_port << " discovery_port "
                  << coordinator.discovery_port << " worker_count "
                  << coordinator.worker_count << " min_worker_count "
                  << coordinator.min_worker_count << " closed "
                  << (coordinator.closed ? 1 : 0) << "\n";
      }
      return EXIT_SUCCESS;
    }

    const auto result = mcpd4::closeCoordinatorDiscovery(
        config.host, config.port, config.token, timeout);
    std::cout << "closed accepted " << (result.accepted ? 1 : 0)
              << " worker_count " << result.worker_count
              << " min_worker_count " << result.min_worker_count << "\n";
  } catch (const std::exception &e) {
    usage(argv[0]);
    std::cerr << "mcpd4_discovery failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
