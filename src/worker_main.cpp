#include <mcpd4/discovery.h>
#include <mcpd4/runtime.h>
#include <mcpd4/status.h>

#include <cerrno>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <iostream>
#include <limits>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/vfs.h>
#include <unistd.h>
#include <vector>

namespace {

#ifndef TMPFS_MAGIC
#define TMPFS_MAGIC 0x01021994
#endif

#ifndef RAMFS_MAGIC
#define RAMFS_MAGIC 0x858458f6
#endif

#ifndef HUGETLBFS_MAGIC
#define HUGETLBFS_MAGIC 0x958458f6
#endif

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
         " [--name NAME]\n"
      << "       [--status-port PORT] [--status-token TOKEN]\n"
      << "       [--rpc-compression none|snappy]\n"
      << "       [--streaming-partitions] [--streaming-dir DIR]\n"
      << "       [--streaming-cache-bytes N]\n"
      << "       [--bk-storage malloc|file_mmap|anon_mmap]\n"
      << "       [--bk-mmap-dir DIR] [--bk-mmap-advise ADVISE]\n";
}

long parsePositiveLong(const std::string &value, const std::string &name) {
  const long parsed = std::stol(value);
  if (parsed <= 0) {
    throw std::runtime_error(name + " must be positive");
  }
  return parsed;
}

std::uint64_t parsePositiveU64(const std::string &value,
                               const std::string &name) {
  const auto parsed = std::stoull(value);
  if (parsed == 0) {
    throw std::runtime_error(name + " must be positive");
  }
  return parsed;
}

std::string statusValue(std::string value) {
  if (value.empty()) {
    return "-";
  }
  for (auto &ch : value) {
    if (std::isspace(static_cast<unsigned char>(ch)) || ch == ',' ||
        ch == ';') {
      ch = '_';
    }
  }
  return value;
}

std::string joinInts(const std::vector<int> &values) {
  if (values.empty()) {
    return "-";
  }
  std::string joined;
  for (const auto value : values) {
    if (!joined.empty()) {
      joined += ",";
    }
    joined += std::to_string(value);
  }
  return joined;
}

std::string envValue(const char *name) {
  const char *value = std::getenv(name);
  return value != nullptr ? std::string(value) : std::string();
}

bool isValidBkStorageMode(const std::string &value) {
  return value == "malloc" || value == "file_mmap" ||
         value == "anon_mmap" || value == "anonymous_mmap";
}

void setEnvValue(const char *name, const std::string &value) {
  if (!value.empty()) {
    setenv(name, value.c_str(), /*overwrite=*/1);
  }
}

std::string effectiveBkStorageMode() {
  const std::string storage = envValue("MCPD3_BK_STORAGE");
  if (!storage.empty()) {
    return storage;
  }
  return "file_mmap";
}

std::string defaultBkMmapDir() {
  return "/var/tmp/mcpd4-bk-mmap-" +
         std::to_string(static_cast<long>(::getpid()));
}

void createDirectoryOrThrow(const std::string &path,
                            const std::string &description) {
  std::error_code ec;
  std::filesystem::create_directories(path, ec);
  if (ec) {
    throw std::runtime_error("failed to create " + description + " " + path +
                             ": " + ec.message());
  }
}

std::string memoryBackedFilesystemName(const std::string &path) {
  struct statfs fs {};
  if (::statfs(path.c_str(), &fs) != 0) {
    throw std::runtime_error("failed to inspect BK mmap directory " + path +
                             ": " + std::strerror(errno));
  }
  switch (static_cast<unsigned long>(fs.f_type)) {
  case TMPFS_MAGIC:
    return "tmpfs";
  case RAMFS_MAGIC:
    return "ramfs";
  case HUGETLBFS_MAGIC:
    return "hugetlbfs";
  default:
    return {};
  }
}

void requireDiskBackedBkMmapDir(const std::string &path) {
  const std::string fs_name = memoryBackedFilesystemName(path);
  if (!fs_name.empty()) {
    throw std::runtime_error(
        "BK mmap directory " + path + " is on memory-backed filesystem " +
        fs_name + "; choose a disk-backed --bk-mmap-dir");
  }
}

class OwnedDirectory {
public:
  void create(const std::string &path) {
    if (path.empty()) {
      return;
    }
    createDirectoryOrThrow(path, "BK mmap directory");
    path_ = path;
  }

  ~OwnedDirectory() {
    if (path_.empty()) {
      return;
    }
    std::error_code ec;
    std::filesystem::remove(path_, ec);
  }

  OwnedDirectory(const OwnedDirectory &) = delete;
  OwnedDirectory &operator=(const OwnedDirectory &) = delete;
  OwnedDirectory() = default;

private:
  std::string path_;
};

void configureBkStorageDefaults(std::string *bk_storage,
                                std::string *bk_mmap_dir,
                                OwnedDirectory *owned_mmap_dir) {
  if (bk_storage->empty() && !bk_mmap_dir->empty()) {
    *bk_storage = "file_mmap";
  }

  const bool explicit_storage = !bk_storage->empty();
  const bool env_storage = !envValue("MCPD3_BK_STORAGE").empty();
  const bool env_mmap_dir = !envValue("MCPD3_BK_MMAP_DIR").empty();

  if (!explicit_storage && !env_storage) {
    *bk_storage = "file_mmap";
  }

  const std::string effective_storage =
      !bk_storage->empty() ? *bk_storage : envValue("MCPD3_BK_STORAGE");
  if (effective_storage == "file_mmap" && bk_mmap_dir->empty() &&
      !env_mmap_dir) {
    *bk_mmap_dir = defaultBkMmapDir();
    owned_mmap_dir->create(*bk_mmap_dir);
    requireDiskBackedBkMmapDir(*bk_mmap_dir);
  } else if (effective_storage == "file_mmap" && !bk_mmap_dir->empty()) {
    createDirectoryOrThrow(*bk_mmap_dir, "BK mmap directory");
    requireDiskBackedBkMmapDir(*bk_mmap_dir);
  } else if (effective_storage == "file_mmap" && env_mmap_dir) {
    requireDiskBackedBkMmapDir(envValue("MCPD3_BK_MMAP_DIR"));
  }
}

struct WorkerStatusState {
  mutable std::mutex mutex;
  std::string phase = "starting";
  std::string name;
  std::uint32_t cpu_count = 0;
  std::uint64_t ram_gb = 0;
  std::string temp_path = "-";
  std::string coordinator_host = "-";
  std::uint16_t coordinator_port = 0;
  std::string rpc_compression = "none";
  std::string worker_storage_mode = "memory";
  std::string streaming_dir = "-";
  std::uint64_t streaming_cache_bytes = 0;
  std::string bk_storage = "malloc";
  std::string bk_mmap_dir = "-";
  std::string bk_mmap_advise = "-";
  std::uint16_t status_port = 0;
  long loaded_partition_count = 0;
  std::vector<int> partition_ids;
  int current_load_partition_id = -1;
  std::uint64_t current_load_logical_bytes = 0;
  int current_load_local_node_count = 0;
  std::uint64_t current_load_arc_int_count = 0;
  std::uint64_t current_load_constraint_endpoint_count = 0;
  int last_loaded_partition_id = -1;
  long current_round_id = 0;
  std::vector<int> current_partition_ids;
  long partition_solve_call_count_total = 0;
  long solve_batch_rpc_count_total = 0;
  std::uint64_t worker_solve_wall_us = 0;
  mcpd4::RpcByteStats rpc_bytes;
  std::string last_error = "-";

  void setIdentity(const mcpd4::HelloMessage &hello) {
    std::lock_guard<std::mutex> lock(mutex);
    name = hello.worker_name;
    cpu_count = hello.cpu_count;
    ram_gb = hello.ram_gb;
    temp_path = hello.temp_path;
  }

  void setCoordinator(const std::string &host, std::uint16_t port) {
    std::lock_guard<std::mutex> lock(mutex);
    coordinator_host = host;
    coordinator_port = port;
  }

  void setStatusPort(std::uint16_t port) {
    std::lock_guard<std::mutex> lock(mutex);
    status_port = port;
  }

  void setCompression(mcpd4::TransportCompression compression) {
    std::lock_guard<std::mutex> lock(mutex);
    rpc_compression = mcpd4::transportCompressionName(compression);
  }

  void setStorageMode(const std::string &mode, const std::string &directory,
                      std::uint64_t cache_bytes) {
    std::lock_guard<std::mutex> lock(mutex);
    worker_storage_mode = mode;
    streaming_dir = directory.empty() ? "-" : directory;
    streaming_cache_bytes = cache_bytes;
  }

  void setBkStorage(const std::string &storage, const std::string &mmap_dir,
                    const std::string &mmap_advise) {
    std::lock_guard<std::mutex> lock(mutex);
    bk_storage = storage.empty() ? "malloc" : storage;
    bk_mmap_dir = mmap_dir.empty() ? "-" : mmap_dir;
    bk_mmap_advise = mmap_advise.empty() ? "-" : mmap_advise;
  }

  void setPhase(const std::string &value) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = value;
  }

  void recordPartitionLoading(const mcpd3::PartitionPackage &package,
                              std::uint64_t logical_frame_bytes) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = "loading_partition";
    current_load_partition_id = package.partition_id;
    current_load_logical_bytes = logical_frame_bytes;
    current_load_local_node_count = package.local_node_count;
    current_load_arc_int_count =
        static_cast<std::uint64_t>(package.arcs.size());
    current_load_constraint_endpoint_count =
        static_cast<std::uint64_t>(package.constraint_endpoints.size());
  }

  void recordPartitionLoaded(int partition_id) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = "connected";
    partition_ids.push_back(partition_id);
    loaded_partition_count = static_cast<long>(partition_ids.size());
    last_loaded_partition_id = partition_id;
    current_load_partition_id = -1;
    current_load_logical_bytes = 0;
    current_load_local_node_count = 0;
    current_load_arc_int_count = 0;
    current_load_constraint_endpoint_count = 0;
  }

  void recordSolveStart(long round_id, const std::vector<int> &ids) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = ids.size() > 1 ? "solving_batch" : "solving_round";
    current_round_id = round_id;
    current_partition_ids = ids;
  }

  void recordSolveDone(std::uint64_t elapsed_us, long partition_count,
                       bool batch) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = "connected";
    worker_solve_wall_us += elapsed_us;
    partition_solve_call_count_total += partition_count;
    if (batch) {
      ++solve_batch_rpc_count_total;
    }
    current_partition_ids.clear();
  }

  void recordScaleObjective(long factor) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = "scaling_objective";
    last_error = "scale_factor_" + std::to_string(factor);
  }

  void recordFrameSent(mcpd4::MessageType type,
                       const mcpd4::FrameTransferStats &transfer) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto bytes = transfer.logical_bytes;
    rpc_bytes.tx_bytes_total += bytes;
    rpc_bytes.tx_wire_bytes_total += transfer.wire_bytes;
    rpc_bytes.compression_wall_us += transfer.compression_wall_us;
    if (transfer.compression_requested) {
      if (transfer.compressed) {
        ++rpc_bytes.tx_compressed_frame_count;
      } else {
        ++rpc_bytes.tx_stored_frame_count;
      }
    }
    switch (type) {
    case mcpd4::MessageType::HELLO:
      rpc_bytes.hello_tx_bytes += bytes;
      break;
    case mcpd4::MessageType::READY:
      rpc_bytes.ready_tx_bytes += bytes;
      break;
    case mcpd4::MessageType::SOLVE_ROUND_RESULT:
    case mcpd4::MessageType::SOLVE_ROUND_BATCH_RESULT:
      rpc_bytes.solve_result_tx_bytes += bytes;
      break;
    case mcpd4::MessageType::FULL_LABELS_CHUNK:
    case mcpd4::MessageType::FULL_LABELS_END:
      rpc_bytes.full_labels_result_tx_bytes += bytes;
      ++rpc_bytes.full_labels_result_tx_frame_count;
      break;
    case mcpd4::MessageType::ERROR:
      rpc_bytes.error_tx_bytes += bytes;
      break;
    case mcpd4::MessageType::PARTITION_PACKAGE:
    case mcpd4::MessageType::PARTITION_PACKAGE_BEGIN:
    case mcpd4::MessageType::PARTITION_PACKAGE_CHUNK:
    case mcpd4::MessageType::PARTITION_PACKAGE_END:
    case mcpd4::MessageType::SOLVE_ROUND_REQUEST:
    case mcpd4::MessageType::SCALE_OBJECTIVE:
    case mcpd4::MessageType::REPLACE_PARTITION_CAPACITIES:
    case mcpd4::MessageType::ALPHA_UPDATE:
    case mcpd4::MessageType::STOP:
    case mcpd4::MessageType::SOLVE_ROUND_BATCH_REQUEST:
    case mcpd4::MessageType::FULL_LABELS_REQUEST:
      break;
    }
  }

  void recordFrameReceived(mcpd4::MessageType type,
                           const mcpd4::FrameTransferStats &transfer) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto bytes = transfer.logical_bytes;
    rpc_bytes.rx_bytes_total += bytes;
    rpc_bytes.rx_wire_bytes_total += transfer.wire_bytes;
    rpc_bytes.decompression_wall_us += transfer.decompression_wall_us;
    if (transfer.compression_requested) {
      if (transfer.compressed) {
        ++rpc_bytes.rx_compressed_frame_count;
      } else {
        ++rpc_bytes.rx_stored_frame_count;
      }
    }
    switch (type) {
    case mcpd4::MessageType::PARTITION_PACKAGE:
    case mcpd4::MessageType::PARTITION_PACKAGE_BEGIN:
    case mcpd4::MessageType::PARTITION_PACKAGE_CHUNK:
    case mcpd4::MessageType::PARTITION_PACKAGE_END:
      rpc_bytes.partition_load_rx_bytes += bytes;
      ++rpc_bytes.partition_load_rx_frame_count;
      break;
    case mcpd4::MessageType::SOLVE_ROUND_REQUEST:
    case mcpd4::MessageType::SOLVE_ROUND_BATCH_REQUEST:
      rpc_bytes.solve_request_rx_bytes += bytes;
      break;
    case mcpd4::MessageType::FULL_LABELS_REQUEST:
      rpc_bytes.full_labels_request_rx_bytes += bytes;
      break;
    case mcpd4::MessageType::SCALE_OBJECTIVE:
      rpc_bytes.scale_objective_rx_bytes += bytes;
      break;
    case mcpd4::MessageType::REPLACE_PARTITION_CAPACITIES:
      rpc_bytes.capacity_update_rx_bytes += bytes;
      break;
    case mcpd4::MessageType::STOP:
      rpc_bytes.stop_rx_bytes += bytes;
      break;
    case mcpd4::MessageType::HELLO:
    case mcpd4::MessageType::READY:
    case mcpd4::MessageType::SOLVE_ROUND_RESULT:
    case mcpd4::MessageType::ALPHA_UPDATE:
    case mcpd4::MessageType::ERROR:
    case mcpd4::MessageType::SOLVE_ROUND_BATCH_RESULT:
    case mcpd4::MessageType::FULL_LABELS_CHUNK:
    case mcpd4::MessageType::FULL_LABELS_END:
      break;
    }
  }

  void recordError(const std::string &message) {
    std::lock_guard<std::mutex> lock(mutex);
    phase = "error";
    last_error = statusValue(message);
  }

  std::string snapshot() const {
    std::lock_guard<std::mutex> lock(mutex);
    std::ostringstream out;
    out << "role worker"
        << " name " << statusValue(name)
        << " cpu_count " << cpu_count
        << " ram_gb " << ram_gb
        << " temp_path " << statusValue(temp_path)
        << " phase " << phase
        << " coordinator_host " << statusValue(coordinator_host)
        << " coordinator_port " << coordinator_port
        << " rpc_compression " << rpc_compression
        << " worker_storage_mode " << worker_storage_mode
        << " streaming_dir " << statusValue(streaming_dir)
        << " streaming_cache_bytes " << streaming_cache_bytes
        << " bk_storage " << statusValue(bk_storage)
        << " bk_mmap_dir " << statusValue(bk_mmap_dir)
        << " bk_mmap_advise " << statusValue(bk_mmap_advise)
        << " status_port " << status_port
        << " loaded_partition_count " << loaded_partition_count
        << " partition_ids " << joinInts(partition_ids)
        << " current_load_partition_id " << current_load_partition_id
        << " current_load_logical_bytes " << current_load_logical_bytes
        << " current_load_local_node_count "
        << current_load_local_node_count
        << " current_load_arc_int_count "
        << current_load_arc_int_count
        << " current_load_constraint_endpoint_count "
        << current_load_constraint_endpoint_count
        << " last_loaded_partition_id " << last_loaded_partition_id
        << " current_round_id " << current_round_id
        << " current_partition_ids " << joinInts(current_partition_ids)
        << " partition_solve_call_count_total "
        << partition_solve_call_count_total
        << " solve_batch_rpc_count_total " << solve_batch_rpc_count_total
        << " worker_solve_wall_us " << worker_solve_wall_us
        << " rpc_tx_bytes_total " << rpc_bytes.tx_bytes_total
        << " rpc_rx_bytes_total " << rpc_bytes.rx_bytes_total
        << " rpc_tx_wire_bytes_total " << rpc_bytes.tx_wire_bytes_total
        << " rpc_rx_wire_bytes_total " << rpc_bytes.rx_wire_bytes_total
        << " rpc_compression_wall_us " << rpc_bytes.compression_wall_us
        << " rpc_decompression_wall_us "
        << rpc_bytes.decompression_wall_us
        << " rpc_tx_compressed_frame_count "
        << rpc_bytes.tx_compressed_frame_count
        << " rpc_tx_stored_frame_count "
        << rpc_bytes.tx_stored_frame_count
        << " rpc_rx_compressed_frame_count "
        << rpc_bytes.rx_compressed_frame_count
        << " rpc_rx_stored_frame_count "
        << rpc_bytes.rx_stored_frame_count
        << " rpc_hello_tx_bytes " << rpc_bytes.hello_tx_bytes
        << " rpc_partition_load_rx_bytes "
        << rpc_bytes.partition_load_rx_bytes
        << " rpc_partition_load_rx_frame_count "
        << rpc_bytes.partition_load_rx_frame_count
        << " rpc_solve_request_rx_bytes "
        << rpc_bytes.solve_request_rx_bytes
        << " rpc_solve_result_tx_bytes "
        << rpc_bytes.solve_result_tx_bytes
        << " rpc_scale_objective_rx_bytes "
        << rpc_bytes.scale_objective_rx_bytes
        << " rpc_capacity_update_rx_bytes "
        << rpc_bytes.capacity_update_rx_bytes
        << " rpc_full_labels_request_rx_bytes "
        << rpc_bytes.full_labels_request_rx_bytes
        << " rpc_full_labels_result_tx_bytes "
        << rpc_bytes.full_labels_result_tx_bytes
        << " rpc_full_labels_result_tx_frame_count "
        << rpc_bytes.full_labels_result_tx_frame_count
        << " rpc_ready_tx_bytes " << rpc_bytes.ready_tx_bytes
        << " rpc_stop_rx_bytes " << rpc_bytes.stop_rx_bytes
        << " rpc_error_tx_bytes " << rpc_bytes.error_tx_bytes
        << " last_error " << statusValue(last_error);
    return out.str();
  }
};

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
    std::uint16_t status_port = 0;
    std::string status_token = mcpd4::kDefaultStatusToken;
    mcpd4::TransportCompression rpc_compression =
        mcpd4::TransportCompression::NONE;
    bool streaming_partitions = false;
    std::string streaming_dir;
    std::uint64_t streaming_cache_bytes = 0;
    std::string bk_storage;
    std::string bk_mmap_dir;
    std::string bk_mmap_advise;

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
      } else if (arg == "--status-port" && i + 1 < argc) {
        status_port = parsePort(argv[++i]);
      } else if (arg == "--status-token" && i + 1 < argc) {
        status_token = argv[++i];
      } else if (arg == "--rpc-compression" && i + 1 < argc) {
        rpc_compression =
            mcpd4::parseTransportCompression(argv[++i]);
      } else if (arg == "--streaming-partitions") {
        streaming_partitions = true;
      } else if (arg == "--streaming-dir" && i + 1 < argc) {
        streaming_dir = argv[++i];
      } else if (arg == "--streaming-cache-bytes" && i + 1 < argc) {
        streaming_cache_bytes =
            parsePositiveU64(argv[++i], "--streaming-cache-bytes");
      } else if (arg == "--bk-storage" && i + 1 < argc) {
        bk_storage = argv[++i];
      } else if (arg == "--bk-mmap-dir" && i + 1 < argc) {
        bk_mmap_dir = argv[++i];
      } else if (arg == "--bk-mmap-advise" && i + 1 < argc) {
        bk_mmap_advise = argv[++i];
      } else {
        usage(argv[0]);
        return EXIT_FAILURE;
      }
    }
    if (!bk_storage.empty() && !isValidBkStorageMode(bk_storage)) {
      throw std::runtime_error("unknown --bk-storage mode: " + bk_storage);
    }
    if (streaming_partitions) {
      if (!bk_storage.empty() && bk_storage != "file_mmap") {
        throw std::runtime_error(
            "--streaming-partitions is a file-mmap compatibility alias and "
            "cannot be combined with --bk-storage " +
            bk_storage);
      }
      if (!streaming_dir.empty()) {
        if (!bk_mmap_dir.empty() && bk_mmap_dir != streaming_dir) {
          throw std::runtime_error(
              "--streaming-dir and --bk-mmap-dir must name the same "
              "directory");
        }
        bk_mmap_dir = streaming_dir;
      }
      bk_storage = "file_mmap";
      std::cerr
          << "warning: --streaming-partitions now aliases complete "
             "file-backed solver storage; partition eviction is disabled\n";
      if (streaming_cache_bytes != 0) {
        std::cerr << "warning: --streaming-cache-bytes is ignored because "
                     "solver state remains persistent\n";
      }
    }
    OwnedDirectory owned_bk_mmap_dir;
    configureBkStorageDefaults(&bk_storage, &bk_mmap_dir,
                               &owned_bk_mmap_dir);
    if (bk_storage == "file_mmap" && bk_mmap_dir.empty() &&
        envValue("MCPD3_BK_MMAP_DIR").empty()) {
      throw std::runtime_error(
          "--bk-storage file_mmap requires --bk-mmap-dir");
    }
    setEnvValue("MCPD3_BK_STORAGE", bk_storage);
    setEnvValue("MCPD3_BK_MMAP_DIR", bk_mmap_dir);
    setEnvValue("MCPD3_BK_MMAP_ADVISE", bk_mmap_advise);

    if (rpc_compression == mcpd4::TransportCompression::SNAPPY &&
        !mcpd4::snappyCompressionAvailable()) {
      throw std::runtime_error(
          "--rpc-compression snappy requested but this binary was built "
          "without Snappy support");
    }

    WorkerStatusState status_state;
    auto hello = mcpd4::makeDefaultHello(worker_name);
    if (rpc_compression == mcpd4::TransportCompression::SNAPPY) {
      hello.feature_bits |= mcpd4::kFeatureSnappyCompression;
    }
    status_state.setIdentity(hello);
    status_state.setCompression(rpc_compression);
    status_state.setStorageMode(
        effectiveBkStorageMode() == "file_mmap" ? "file_mmap" : "resident",
        envValue("MCPD3_BK_MMAP_DIR"), 0);
    status_state.setBkStorage(effectiveBkStorageMode(),
                              envValue("MCPD3_BK_MMAP_DIR"),
                              envValue("MCPD3_BK_MMAP_ADVISE"));
    std::unique_ptr<mcpd4::StatusServer> status_server;
    if (status_port != 0) {
      status_server = std::make_unique<mcpd4::StatusServer>(
          "0.0.0.0", status_port, status_token,
          [&status_state] { return status_state.snapshot(); });
      status_state.setStatusPort(status_server->port());
      std::cout << "status_listening port " << status_server->port() << "\n";
      std::cout.flush();
    }

    if (discover) {
      status_state.setPhase("discovering");
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

    status_state.setCoordinator(host, port);
    status_state.setPhase("connecting");
    mcpd4::WorkerRuntimeStatusHooks hooks;
    hooks.on_phase = [&status_state](const std::string &phase) {
      status_state.setPhase(phase);
    };
    hooks.on_frame_sent =
        [&status_state](mcpd4::MessageType type,
                        const mcpd4::FrameTransferStats &transfer) {
          status_state.recordFrameSent(type, transfer);
        };
    hooks.on_frame_received =
        [&status_state](mcpd4::MessageType type,
                        const mcpd4::FrameTransferStats &transfer) {
          status_state.recordFrameReceived(type, transfer);
        };
    hooks.on_partition_loading =
        [&status_state](const mcpd3::PartitionPackage &package,
                        std::uint64_t logical_frame_bytes) {
          status_state.recordPartitionLoading(package, logical_frame_bytes);
        };
    hooks.on_partition_loaded = [&status_state](int partition_id) {
      status_state.recordPartitionLoaded(partition_id);
    };
    hooks.on_solve_start =
        [&status_state](long round_id, const std::vector<int> &partition_ids) {
          status_state.recordSolveStart(round_id, partition_ids);
        };
    hooks.on_solve_done = [&status_state](std::uint64_t elapsed_us,
                                          long partition_solve_count,
                                          bool batch) {
      status_state.recordSolveDone(elapsed_us, partition_solve_count, batch);
    };
    hooks.on_scale_objective = [&status_state](
                                   long factor,
                                   bool saturate_capacity_overflow) {
      (void)saturate_capacity_overflow;
      status_state.recordScaleObjective(factor);
    };
    hooks.on_error = [&status_state](const std::string &message) {
      status_state.recordError(message);
    };

    mcpd4::WorkerRuntimeOptions runtime_options;
    runtime_options.streaming_partitions = streaming_partitions;
    runtime_options.streaming_directory = envValue("MCPD3_BK_MMAP_DIR");
    runtime_options.streaming_resident_bytes = streaming_cache_bytes;
    const std::string solver_storage_mode = effectiveBkStorageMode();
    if (solver_storage_mode == "file_mmap") {
      runtime_options.solver_storage.mode =
          mcpd3::SolverStorageMode::FILE_BACKED_MMAP;
    } else if (solver_storage_mode == "anon_mmap" ||
               solver_storage_mode == "anonymous_mmap") {
      runtime_options.solver_storage.mode =
          mcpd3::SolverStorageMode::ANONYMOUS_MMAP;
    } else {
      runtime_options.solver_storage.mode =
          mcpd3::SolverStorageMode::RESIDENT;
    }
    runtime_options.solver_storage.directory =
        envValue("MCPD3_BK_MMAP_DIR");
    runtime_options.solver_storage.mmap_advice =
        envValue("MCPD3_BK_MMAP_ADVISE");
    mcpd4::runWorkerClient(host, port, hello, hooks, rpc_compression,
                           runtime_options);
  } catch (const std::exception &e) {
    std::cerr << "mcpd4_worker failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
