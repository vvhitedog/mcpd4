#include <mcpd4/runtime.h>

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
  std::cerr << "usage: " << argv0 << " HOST PORT [--name NAME]\n";
}

} // namespace

int main(int argc, char **argv) {
  try {
    if (argc < 3) {
      usage(argv[0]);
      return EXIT_FAILURE;
    }

    const std::string host = argv[1];
    const std::uint16_t port = parsePort(argv[2]);
    std::string worker_name;
    for (int i = 3; i < argc; ++i) {
      const std::string arg = argv[i];
      if (arg == "--name" && i + 1 < argc) {
        worker_name = argv[++i];
      } else {
        usage(argv[0]);
        return EXIT_FAILURE;
      }
    }

    mcpd4::runWorkerClient(
        host, port, mcpd4::makeDefaultHello(worker_name));
  } catch (const std::exception &e) {
    std::cerr << "mcpd4_worker failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
