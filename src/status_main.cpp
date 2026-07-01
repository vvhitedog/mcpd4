#include <mcpd4/status.h>

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
  std::cerr << "usage: " << argv0
            << " HOST PORT [--token TOKEN] [--timeout-ms N]\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 3) {
      usage(argv[0]);
      return EXIT_FAILURE;
    }

    const std::string host = argv[1];
    const std::uint16_t port = parsePort(argv[2], "port");
    std::string token = mcpd4::kDefaultStatusToken;
    long timeout_ms = 5000;
    for (int i = 3; i < argc; ++i) {
      const std::string arg = argv[i];
      auto require_value = [&](const std::string &name) -> std::string {
        if (i + 1 >= argc) {
          throw std::runtime_error(name + " requires a value");
        }
        return argv[++i];
      };

      if (arg == "--token") {
        token = require_value(arg);
      } else if (arg == "--timeout-ms") {
        timeout_ms = parsePositiveLong(require_value(arg), arg);
      } else {
        throw std::runtime_error("unknown argument: " + arg);
      }
    }

    std::cout << mcpd4::queryStatus(host, port, token,
                                    std::chrono::milliseconds(timeout_ms))
              << "\n";
  } catch (const std::exception &e) {
    usage(argv[0]);
    std::cerr << "mcpd4_status failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
