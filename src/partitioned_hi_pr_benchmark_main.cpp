#include <mcpd4/partitioned_hi_pr.h>

#include <graph/dimacs.h>
#include <graph/partition.h>
#include <maxflow/graph.h>

#include <algorithm>
#include <chrono>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using Clock = std::chrono::steady_clock;
using BkGraph = Graph<mcpd3::Capacity, mcpd3::Objective, mcpd3::Objective>;
using mcpd4::experimental::DirectedArc;

struct Config {
  std::string dimacs_path;
  int partition_count = 2;
  int partitioning_count = 1;
  int repeats = 3;
  std::size_t max_rounds = 100000;
  double global_relabel_work_factor = 0.0;
  std::string partitioner = "basic";
  bool directed = false;
  bool symmetric_streaming = false;
  bool validate = false;
};

std::uint64_t elapsedUs(Clock::time_point start) {
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::microseconds>(Clock::now() - start)
          .count());
}

int parsePositiveInt(const std::string &text, const std::string &option) {
  char *end = nullptr;
  const long parsed = std::strtol(text.c_str(), &end, 10);
  if (end == text.c_str() || *end != '\0' || parsed <= 0 ||
      parsed > std::numeric_limits<int>::max()) {
    throw std::invalid_argument(option + " requires a positive integer");
  }
  return static_cast<int>(parsed);
}

double parseNonnegativeDouble(const std::string &text,
                              const std::string &option) {
  errno = 0;
  char *end = nullptr;
  const double parsed = std::strtod(text.c_str(), &end);
  if (errno != 0 || end == text.c_str() || *end != '\0' || parsed < 0) {
    throw std::invalid_argument(option + " requires a nonnegative number");
  }
  return parsed;
}

Config parseArgs(int argc, char **argv) {
  if (argc < 2) {
    throw std::invalid_argument(
        "usage: mcpd4_partitioned_hi_pr_benchmark DIMACS "
        "[--directed|--symmetric-streaming] [--partitions N] "
        "[--partitionings N] "
        "[--partitioner basic|metis] [--repeats N] [--max-rounds N] "
        "[--global-relabel-work-factor F] [--validate]");
  }
  Config config;
  config.dimacs_path = argv[1];
  for (int index = 2; index < argc; ++index) {
    const std::string option = argv[index];
    auto value = [&]() -> std::string {
      if (++index >= argc) {
        throw std::invalid_argument(option + " requires a value");
      }
      return argv[index];
    };
    if (option == "--partitions") {
      config.partition_count = parsePositiveInt(value(), option);
    } else if (option == "--partitionings") {
      config.partitioning_count = parsePositiveInt(value(), option);
    } else if (option == "--repeats") {
      config.repeats = parsePositiveInt(value(), option);
    } else if (option == "--max-rounds") {
      config.max_rounds =
          static_cast<std::size_t>(parsePositiveInt(value(), option));
    } else if (option == "--global-relabel-work-factor") {
      config.global_relabel_work_factor =
          parseNonnegativeDouble(value(), option);
    } else if (option == "--partitioner") {
      config.partitioner = value();
      if (config.partitioner != "basic" && config.partitioner != "metis") {
        throw std::invalid_argument("partitioner must be basic or metis");
      }
    } else if (option == "--directed") {
      config.directed = true;
    } else if (option == "--symmetric-streaming") {
      config.symmetric_streaming = true;
    } else if (option == "--validate") {
      config.validate = true;
    } else {
      throw std::invalid_argument("unknown option: " + option);
    }
  }
  if (config.directed && config.symmetric_streaming) {
    throw std::invalid_argument(
        "directed and symmetric-streaming input modes are mutually exclusive");
  }
  if (config.partitioning_count > 1 && config.partitioner != "basic") {
    throw std::invalid_argument(
        "multiple partitionings currently require the basic partitioner");
  }
  return config;
}

mcpd3::MinCutGraph readGraph(const Config &config) {
  if (config.directed) {
    return mcpd3::read_dimacs_directed_streaming(config.dimacs_path);
  }
  if (config.symmetric_streaming) {
    return mcpd3::read_dimacs_symmetric_streaming(config.dimacs_path);
  }
  return mcpd3::read_dimacs(config.dimacs_path);
}

std::vector<int> partitionGraph(const Config &config,
                                const mcpd3::MinCutGraph &graph) {
  if (config.partitioner == "basic") {
    return mcpd3::basic_graph_partition(config.partition_count, graph);
  }
#ifdef HAVE_METIS
  return mcpd3::metis_partition(config.partition_count, graph);
#else
  (void)graph;
  throw std::runtime_error(
      "this benchmark was built without METIS; use --partitioner basic");
#endif
}

std::vector<DirectedArc> makeExplicitGraph(const mcpd3::MinCutGraph &graph,
                                           int source, int sink) {
  std::vector<DirectedArc> arcs;
  arcs.reserve(static_cast<std::size_t>(2) * graph.narc + graph.nnode);
  for (int index = 0; index < graph.narc; ++index) {
    const int tail = graph.arcs[2 * index];
    const int head = graph.arcs[2 * index + 1];
    const auto forward = graph.arc_capacities[2 * index];
    const auto reverse = graph.arc_capacities[2 * index + 1];
    if (forward > 0) {
      arcs.push_back({tail, head, forward});
    }
    if (reverse > 0) {
      arcs.push_back({head, tail, reverse});
    }
  }
  for (int node = 0; node < graph.nnode; ++node) {
    const auto terminal = graph.terminal_capacities[node];
    if (terminal > 0) {
      arcs.push_back({source, node, terminal});
    } else if (terminal < 0) {
      arcs.push_back({node, sink, mcpd3::narrow_objective_to_capacity(
                                      mcpd3::absolute_capacity(terminal))});
    }
  }
  return arcs;
}

mcpd3::Objective solveBk(const mcpd3::MinCutGraph &graph) {
  BkGraph solver(graph.nnode, graph.narc);
  solver.add_node(graph.nnode);
  for (int index = 0; index < graph.narc; ++index) {
    solver.add_edge(graph.arcs[2 * index], graph.arcs[2 * index + 1],
                    graph.arc_capacities[2 * index],
                    graph.arc_capacities[2 * index + 1]);
  }
  for (int node = 0; node < graph.nnode; ++node) {
    const auto terminal = mcpd3::widen_capacity(graph.terminal_capacities[node]);
    solver.add_tweights(node, std::max<mcpd3::Objective>(0, terminal),
                        std::max<mcpd3::Objective>(0, -terminal));
  }
  return solver.maxflow();
}

std::uint64_t median(std::vector<std::uint64_t> values) {
  std::sort(values.begin(), values.end());
  return values[values.size() / 2];
}

} // namespace

int main(int argc, char **argv) {
  try {
    std::cout << std::unitbuf;
    const Config config = parseArgs(argc, argv);

    const auto read_start = Clock::now();
    const mcpd3::MinCutGraph graph = readGraph(config);
    const std::uint64_t read_wall_us = elapsedUs(read_start);

    const int source = graph.nnode;
    const int sink = graph.nnode + 1;
    const std::vector<DirectedArc> explicit_arcs =
        makeExplicitGraph(graph, source, sink);

    const auto partition_start = Clock::now();
    std::vector<std::vector<int>> partitionings;
    if (config.partitioning_count == 1) {
      std::vector<int> partitions = partitionGraph(config, graph);
      partitions.push_back(-1);
      partitions.push_back(-1);
      partitionings.push_back(std::move(partitions));
    } else {
      partitionings =
          mcpd4::experimental::makeSeparatedContiguousPartitionCover(
              graph.nnode + 2, source, sink, explicit_arcs,
              config.partition_count, config.partitioning_count);
    }
    const std::uint64_t partition_wall_us = elapsedUs(partition_start);
    const auto cover_geometry =
        mcpd4::experimental::analyzePartitionCoverGeometry(
            graph.nnode + 2, source, sink, explicit_arcs, partitionings);

    std::vector<std::vector<std::size_t>> partition_sizes(
        partitionings.size(),
        std::vector<std::size_t>(config.partition_count, 0));
    for (std::size_t cover = 0; cover < partitionings.size(); ++cover) {
      for (int node = 0; node < graph.nnode; ++node) {
        const int partition = partitionings[cover][node];
        if (partition < 0 || partition >= config.partition_count) {
          throw std::runtime_error(
              "partitioner returned an invalid partition id");
        }
        ++partition_sizes[cover][partition];
      }
    }

    std::cout << "capacity_mode " << mcpd3::capacity_mode_name() << '\n';
    std::cout << "graph_node_count " << graph.nnode << '\n';
    std::cout << "graph_edge_pair_count " << graph.narc << '\n';
    std::cout << "explicit_directed_arc_count " << explicit_arcs.size() << '\n';
    std::cout << "partition_count " << config.partition_count << '\n';
    std::cout << "partitioning_count " << config.partitioning_count << '\n';
    std::cout << "partitioner " << config.partitioner << '\n';
    std::cout << "global_relabel_work_factor "
              << config.global_relabel_work_factor << '\n';
    std::cout << "cover_boundary_node_counts";
    for (const auto count : cover_geometry.boundary_node_counts) {
      std::cout << ' ' << count;
    }
    std::cout << '\n';
    std::cout << "cover_uncovered_node_count "
              << cover_geometry.uncovered_node_count << '\n';
    std::cout << "cover_uncovered_directed_arc_count "
              << cover_geometry.uncovered_directed_arc_count << '\n';
    std::cout << "cover_minimum_pairwise_boundary_distance "
              << cover_geometry.minimum_pairwise_boundary_distance << '\n';
    for (std::size_t cover = 0; cover < partition_sizes.size(); ++cover) {
      std::cout << "partition_sizes cover=" << cover;
      for (const auto size : partition_sizes[cover]) {
        std::cout << ' ' << size;
      }
      std::cout << '\n';
    }
    std::cout << "timing_read_wall_us " << read_wall_us << '\n';
    std::cout << "timing_partition_wall_us " << partition_wall_us << '\n';

    std::vector<std::uint64_t> bk_times;
    std::vector<std::uint64_t> partitioned_times;
    mcpd3::Objective exact_value = 0;
    for (int repeat = 0; repeat < config.repeats; ++repeat) {
      const auto bk_start = Clock::now();
      const auto current_exact = solveBk(graph);
      const std::uint64_t bk_wall_us = elapsedUs(bk_start);
      if (repeat != 0 && current_exact != exact_value) {
        throw std::logic_error("BK objective changed between repeats");
      }
      exact_value = current_exact;
      bk_times.push_back(bk_wall_us);

      mcpd4::experimental::PartitionedHiPrOptions options;
      options.max_coordination_rounds = config.max_rounds;
      options.global_relabel_work_factor =
          config.global_relabel_work_factor;
      options.validate_invariants = config.validate;
      const auto partitioned_start = Clock::now();
      const auto result =
          config.partitioning_count == 1
              ? mcpd4::experimental::partitionedHiPr(
                    graph.nnode + 2, source, sink, explicit_arcs,
                    partitionings.front(), options)
              : mcpd4::experimental::partitionCoverHiPr(
                    graph.nnode + 2, source, sink, explicit_arcs,
                    partitionings, options);
      const std::uint64_t partitioned_wall_us = elapsedUs(partitioned_start);
      partitioned_times.push_back(partitioned_wall_us);

      std::cout << "run " << repeat << " bk_wall_us " << bk_wall_us
                << " partitioned_wall_us " << partitioned_wall_us
                << " converged " << result.converged
                << " exact_value " << mcpd3::integer_to_string(exact_value)
                << " partitioned_value "
                << mcpd3::integer_to_string(result.maximum_preflow)
                << " cut_value "
                << mcpd3::integer_to_string(result.cut_capacity)
                << " coordination_rounds "
                << result.stats.coordination_rounds
                << " partition_cover_cycles "
                << result.stats.partition_cover_cycles
                << " partition_local_phases "
                << result.stats.partition_local_phases
                << " global_relabels " << result.stats.global_relabels
                << " local_pushes " << result.stats.local_pushes
                << " boundary_pushes " << result.stats.boundary_pushes
                << " local_relabels " << result.stats.local_relabels
                << " local_arc_scans " << result.stats.local_arc_scans
                << " work_global_relabel_interruptions "
                << result.stats.work_global_relabel_interruptions
                << " gap_events " << result.stats.gap_events
                << " retired_by_gap " << result.stats.retired_by_gap
                << " boundary_directed_arcs "
                << result.stats.boundary_directed_arc_count
                << " boundary_nodes " << result.stats.boundary_node_count
                << " minimum_boundary_nodes "
                << result.stats.minimum_boundary_node_count
                << " maximum_boundary_nodes "
                << result.stats.maximum_boundary_node_count
                << " global_relabel_wall_us "
                << result.stats.global_relabel_wall_us
                << " boundary_push_wall_us "
                << result.stats.boundary_push_wall_us
                << " local_discharge_wall_us "
                << result.stats.local_discharge_wall_us << '\n';
      if (!result.converged || result.maximum_preflow != exact_value ||
          result.cut_capacity != exact_value) {
        throw std::runtime_error(
            "partitioned hi_pr failed to reproduce the exact BK objective");
      }
    }

    const auto bk_median = median(bk_times);
    const auto partitioned_median = median(partitioned_times);
    std::cout << "summary_exact_value "
              << mcpd3::integer_to_string(exact_value) << '\n';
    std::cout << "summary_bk_median_wall_us " << bk_median << '\n';
    std::cout << "summary_partitioned_median_wall_us " << partitioned_median
              << '\n';
    std::cout << "summary_partitioned_over_bk "
              << (bk_median == 0
                      ? 0.0
                      : static_cast<double>(partitioned_median) / bk_median)
              << '\n';
  } catch (const std::exception &error) {
    std::cerr << "mcpd4_partitioned_hi_pr_benchmark failed: " << error.what()
              << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
