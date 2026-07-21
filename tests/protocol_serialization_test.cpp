#include <chrono>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <mcpd4/delta_codec.h>
#include <mcpd4/integer_codec.h>
#include <mcpd4/protocol.h>

namespace {

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename Fn> void requireThrows(Fn fn, const std::string &message) {
  bool threw = false;
  try {
    fn();
  } catch (const std::exception &) {
    threw = true;
  }
  require(threw, message);
}

void requireAlphaUpdateEqual(const mcpd3::AlphaUpdate &lhs,
                             const mcpd3::AlphaUpdate &rhs) {
  require(lhs.constraint_id == rhs.constraint_id,
          "alpha update constraint id mismatch");
  require(lhs.alpha == rhs.alpha, "alpha update alpha mismatch");
  require(lhs.last_alpha == rhs.last_alpha,
          "alpha update last alpha mismatch");
  require(lhs.alpha_momentum == rhs.alpha_momentum,
          "alpha update momentum mismatch");
}

void requireCompactAlphaUpdateEqual(const mcpd3::AlphaUpdate &lhs,
                                    const mcpd3::AlphaUpdate &rhs) {
  require(lhs.constraint_id == rhs.constraint_id,
          "alpha update constraint id mismatch");
  require(lhs.alpha == rhs.alpha, "alpha update alpha mismatch");
  require(lhs.last_alpha == 0,
          "compact alpha update should omit last alpha");
  require(lhs.alpha_momentum == 0,
          "compact alpha update should omit momentum");
}

void requireEndpointEqual(const mcpd3::ConstraintEndpointBinding &lhs,
                          const mcpd3::ConstraintEndpointBinding &rhs) {
  require(lhs.constraint_id == rhs.constraint_id,
          "endpoint constraint id mismatch");
  require(lhs.global_node_id == rhs.global_node_id,
          "endpoint global node id mismatch");
  require(lhs.local_index == rhs.local_index, "endpoint local index mismatch");
  require(lhs.is_source == rhs.is_source, "endpoint side mismatch");
  require(lhs.alpha == rhs.alpha, "endpoint alpha mismatch");
  require(lhs.last_alpha == rhs.last_alpha, "endpoint last alpha mismatch");
  require(lhs.alpha_momentum == rhs.alpha_momentum,
          "endpoint alpha momentum mismatch");
}

void requireLabelEqual(const mcpd3::ConstraintLabel &lhs,
                       const mcpd3::ConstraintLabel &rhs) {
  require(lhs.constraint_id == rhs.constraint_id, "label constraint mismatch");
  require(lhs.global_node_id == rhs.global_node_id,
          "label global node mismatch");
  require(lhs.local_index == rhs.local_index, "label local index mismatch");
  require(lhs.label == rhs.label, "label value mismatch");
}

void requireCompactLabelEqual(const mcpd3::ConstraintLabel &lhs,
                              const mcpd3::ConstraintLabel &rhs) {
  require(lhs.constraint_id == rhs.constraint_id, "label constraint mismatch");
  require(lhs.global_node_id == -1,
          "compact result label should omit global node metadata");
  require(lhs.local_index == -1,
          "compact result label should omit local index metadata");
  require(lhs.label == rhs.label, "label value mismatch");
}

void requireNodeLabelEqual(const mcpd3::NodeLabel &lhs,
                           const mcpd3::NodeLabel &rhs) {
  require(lhs.global_node_id == rhs.global_node_id,
          "node label global node mismatch");
  require(lhs.local_index == rhs.local_index,
          "node label local index mismatch");
  require(lhs.label == rhs.label, "node label value mismatch");
}

void requirePackageEqual(const mcpd3::PartitionPackage &lhs,
                         const mcpd3::PartitionPackage &rhs) {
  require(lhs.partition_id == rhs.partition_id, "package partition mismatch");
  require(lhs.local_node_count == rhs.local_node_count,
          "package local node count mismatch");
  require(lhs.arcs == rhs.arcs, "package arcs mismatch");
  require(lhs.arc_capacities == rhs.arc_capacities,
          "package arc capacities mismatch");
  require(lhs.terminal_capacities == rhs.terminal_capacities,
          "package terminal capacities mismatch");
  require(lhs.local_to_global == rhs.local_to_global,
          "package local-to-global mismatch");
  require(lhs.objective_multiplier == rhs.objective_multiplier,
          "package objective multiplier mismatch");
  require(lhs.canonical_cut_selection == rhs.canonical_cut_selection,
          "package canonical cut selection mismatch");
  require(lhs.force_full_mincut_recompute == rhs.force_full_mincut_recompute,
          "package full recompute policy mismatch");
  require(lhs.reference_cut_labels == rhs.reference_cut_labels,
          "package reference labels mismatch");
  require(lhs.reference_cut_selection == rhs.reference_cut_selection,
          "package reference selection mismatch");
  require(lhs.reference_cut_check_interval ==
              rhs.reference_cut_check_interval,
          "package reference check interval mismatch");
  require(lhs.constraint_endpoints.size() == rhs.constraint_endpoints.size(),
          "package endpoint count mismatch");
  for (size_t i = 0; i < lhs.constraint_endpoints.size(); ++i) {
    requireEndpointEqual(lhs.constraint_endpoints[i],
                         rhs.constraint_endpoints[i]);
  }
}

mcpd3::PartitionPackage makePackage() {
  mcpd3::PartitionPackage package;
  package.partition_id = 7;
  package.local_node_count = 3;
  package.arcs = {0, 1, 1, 2};
  package.arc_capacities = {3, 5, 7, 11};
  package.terminal_capacities = {13, -17, 19};
  package.local_to_global = {100, 200, 300};
  package.objective_multiplier = 6;
  package.canonical_cut_selection =
      mcpd3::CanonicalCutSelection::MAXIMUM_LABELS;
  package.force_full_mincut_recompute = true;
  package.reference_cut_labels = {1, 0, 1};
  package.reference_cut_selection =
      mcpd3::ReferenceCutSelection::EXACT_REFERENCE_IF_OPTIMAL;
  package.reference_cut_check_interval = 7;
  package.constraint_endpoints.push_back(
      mcpd3::ConstraintEndpointBinding{/*constraint_id=*/41,
                                        /*global_node_id=*/200,
                                        /*local_index=*/1,
                                        /*is_source=*/true,
                                        /*alpha=*/-23,
                                        /*last_alpha=*/29,
                                        /*alpha_momentum=*/1.25f});
  package.constraint_endpoints.push_back(
      mcpd3::ConstraintEndpointBinding{/*constraint_id=*/42,
                                        /*global_node_id=*/300,
                                        /*local_index=*/2,
                                        /*is_source=*/false,
                                        /*alpha=*/31,
                                        /*last_alpha=*/-37,
                                        /*alpha_momentum=*/-0.5f});
  return package;
}

void frameHeaderIsLittleEndian() {
  const std::vector<std::uint8_t> payload{0xaa, 0xbb};
  const auto frame = mcpd4::encodeFrame(
      mcpd4::MessageType::PARTITION_PACKAGE, payload);

  require(frame.size() == 14, "frame size mismatch");
  require(frame[0] == 2 && frame[1] == 0 && frame[2] == 0 && frame[3] == 0,
          "message type should be little-endian uint32");
  require(frame[4] == 2 && frame[5] == 0 && frame[6] == 0 && frame[7] == 0 &&
              frame[8] == 0 && frame[9] == 0 && frame[10] == 0 &&
              frame[11] == 0,
          "payload length should be little-endian uint64");

  const auto decoded = mcpd4::decodeFrame(frame);
  require(decoded.type == mcpd4::MessageType::PARTITION_PACKAGE,
          "decoded frame type mismatch");
  require(decoded.payload == payload, "decoded frame payload mismatch");
}

void roundTripsHello() {
  mcpd4::HelloMessage message;
  message.protocol_version = 3;
  message.capacity_mode = mcpd4::CapacityMode::BITS_128;
  message.worker_name = "worker-a";
  message.cpu_count = 16;
  message.ram_gb = 64;
  message.feature_bits = 0x1020;
  message.temp_path = "/tmp/mcpd4";
  message.debug_build = true;
  message.little_endian = true;

  const auto decoded = mcpd4::decodeHello(
      mcpd4::encodeHello(message));
  require(decoded.protocol_version == message.protocol_version,
          "hello protocol version mismatch");
  require(decoded.capacity_mode == message.capacity_mode,
          "hello capacity mode mismatch");
  require(decoded.worker_name == message.worker_name,
          "hello worker name mismatch");
  require(decoded.cpu_count == message.cpu_count, "hello CPU count mismatch");
  require(decoded.ram_gb == message.ram_gb, "hello RAM mismatch");
  require(decoded.feature_bits == message.feature_bits,
          "hello feature bits mismatch");
  require(decoded.temp_path == message.temp_path, "hello temp path mismatch");
  require(decoded.debug_build == message.debug_build,
          "hello debug build mismatch");
  require(decoded.little_endian == message.little_endian,
          "hello endianness mismatch");
}

void roundTripsConfiguredPrecisionExtremes() {
  const mcpd3::Capacity extreme = mcpd3::capacity_test_extreme_value();
  const mcpd3::Objective objective_extreme = mcpd3::checked_add(
      mcpd3::widen_capacity(extreme), mcpd3::widen_capacity(extreme));
  const mcpd3::Lagrange alpha_extreme = objective_extreme;

  auto package = makePackage();
  package.arc_capacities = {extreme, -extreme, extreme, -extreme};
  package.terminal_capacities = {extreme, -extreme, 0};
  package.constraint_endpoints[0].alpha = alpha_extreme;
  package.constraint_endpoints[0].last_alpha = -alpha_extreme;
  package.constraint_endpoints[1].alpha = -alpha_extreme;
  package.constraint_endpoints[1].last_alpha = alpha_extreme;
  requirePackageEqual(
      mcpd4::decodePartitionPackage(mcpd4::encodePartitionPackage(package)),
      package);

  mcpd3::PartitionSolveRequest request;
  request.round_id = 1;
  request.partition_id = package.partition_id;
  request.scale = 1;
  request.regularization_strength = extreme;
  request.alpha_updates = {
      mcpd3::AlphaUpdate{/*constraint_id=*/1, /*alpha=*/alpha_extreme,
                         /*last_alpha=*/0, /*alpha_momentum=*/0},
      mcpd3::AlphaUpdate{/*constraint_id=*/2, /*alpha=*/-alpha_extreme,
                         /*last_alpha=*/0, /*alpha_momentum=*/0}};
  const auto decoded_request =
      mcpd4::decodeSolveRoundRequest(mcpd4::encodeSolveRoundRequest(request));
  require(decoded_request.regularization_strength == extreme,
          "extreme regularization strength mismatch");
  require(decoded_request.alpha_updates[0].alpha == alpha_extreme &&
              decoded_request.alpha_updates[1].alpha == -alpha_extreme,
          "widened stateless alpha mismatch");

  mcpd3::PartitionSolveResult result;
  result.round_id = 1;
  result.partition_id = package.partition_id;
  result.lower_bound = -objective_extreme;
  result.regularization_budget = objective_extreme;
  result.regularization_contribution = objective_extreme - 1;
  const auto decoded_result =
      mcpd4::decodeSolveRoundResult(mcpd4::encodeSolveRoundResult(result));
  require(decoded_result.lower_bound == result.lower_bound,
          "extreme lower bound mismatch");
  require(decoded_result.regularization_budget == result.regularization_budget,
          "extreme regularization budget mismatch");
  require(decoded_result.regularization_contribution ==
              result.regularization_contribution,
          "extreme regularization contribution mismatch");

  mcpd4::TemporalSolveCodecState encoder;
  mcpd4::TemporalSolveCodecState decoder;
  const auto first = mcpd4::decodeDeltaSolveRoundRequest(
      mcpd4::encodeDeltaSolveRoundRequest(request, &encoder), &decoder);
  require(first.alpha_updates[0].alpha == alpha_extreme &&
              first.alpha_updates[1].alpha == -alpha_extreme,
          "widened temporal alpha initial sync mismatch");
  request.round_id = 2;
  request.alpha_updates[0].alpha = -alpha_extreme;
  request.alpha_updates[1].alpha = alpha_extreme;
  const auto changed = mcpd4::decodeDeltaSolveRoundRequest(
      mcpd4::encodeDeltaSolveRoundRequest(request, &encoder), &decoder);
  require(changed.alpha_updates[0].alpha == -alpha_extreme &&
              changed.alpha_updates[1].alpha == alpha_extreme,
          "widened temporal alpha sign-change mismatch");

  mcpd4::TemporalSolveCodecState result_encoder;
  mcpd4::TemporalSolveCodecState result_decoder;
  const auto delta_result = mcpd4::decodeDeltaTimedSolveRoundResult(
      mcpd4::encodeDeltaSolveRoundResultWithTiming(
          result, /*worker_solve_wall_us=*/7, &result_encoder),
      &result_decoder);
  require(delta_result.result.lower_bound == result.lower_bound &&
              delta_result.result.regularization_budget ==
                  result.regularization_budget &&
              delta_result.result.regularization_contribution ==
                  result.regularization_contribution,
          "extreme temporal result objective mismatch");
}

void rejectsNonCanonicalAndOutOfRangeIntegers() {
  std::vector<std::uint8_t> non_canonical{0x80, 0x00};
  std::size_t offset = 0;
  requireThrows(
      [&] { (void)mcpd4::integer_codec::readSigned(non_canonical, &offset); },
      "overlong integer varints must be rejected");

  if (mcpd3::capacity_is_bounded()) {
    auto outside = mcpd4::integer_codec::toCppInt(
        mcpd3::capacity_test_extreme_value());
    ++outside;
    requireThrows(
        [&] { (void)mcpd4::integer_codec::capacityFromCppInt(outside); },
        "capacity decoder must reject values outside configured precision");
  }
}

void roundTripsPartitionPackage() {
  const auto message = makePackage();
  const auto decoded = mcpd4::decodePartitionPackage(
      mcpd4::encodePartitionPackage(message));
  requirePackageEqual(decoded, message);
}

void roundTripsMappedMultipartPartitionPackage() {
  const auto message = makePackage();
  const auto header = mcpd4::makePartitionPackageTransferHeader(message);
  const auto decoded_header = mcpd4::decodePartitionPackageTransferBegin(
      mcpd4::encodePartitionPackageTransferBegin(header));
  require(decoded_header.partition_id == message.partition_id,
          "multipart header partition mismatch");
  require(decoded_header.section_counts == header.section_counts,
          "multipart header section counts mismatch");

  const auto scratch =
      std::filesystem::temp_directory_path() /
      ("mcpd4-multipart-protocol-test-" +
       std::to_string(std::chrono::steady_clock::now()
                          .time_since_epoch()
                          .count()));
  std::filesystem::create_directories(scratch);
  mcpd3::SolverStorageOptions storage;
  storage.mode = mcpd3::SolverStorageMode::FILE_BACKED_MMAP;
  storage.directory = scratch.string();
  mcpd4::PartitionPackageAssembler assembler(decoded_header, storage);
  for (std::size_t section_index = 0;
       section_index < mcpd4::kPartitionPackageSectionCount;
       ++section_index) {
    const auto section =
        static_cast<mcpd4::PartitionPackageSection>(section_index);
    const auto count = header.section_counts[section_index];
    for (std::uint64_t offset = 0; offset < count; ++offset) {
      auto chunk = mcpd4::decodePartitionPackageTransferChunk(
          mcpd4::encodePartitionPackageTransferChunk(
              message, section, offset, /*count=*/1));
      assembler.append(std::move(chunk));
    }
  }
  require(assembler.complete(), "multipart package should be complete");
  auto decoded = assembler.finish();
  requirePackageEqual(decoded, message);
  require(decoded.arcs.isFileBacked() &&
              decoded.arc_capacities.isFileBacked() &&
              decoded.terminal_capacities.isFileBacked() &&
              decoded.local_to_global.isFileBacked() &&
              decoded.reference_cut_labels.isFileBacked(),
          "multipart decoder should assemble arrays directly in mappings");
  const auto end = mcpd4::decodePartitionPackageTransferEnd(
      mcpd4::encodePartitionPackageTransferEnd(
          mcpd4::PartitionPackageTransferEnd{message.partition_id}));
  require(end.partition_id == message.partition_id,
          "multipart end partition mismatch");
  std::filesystem::remove_all(scratch);
}

void rejectsMalformedMultipartPartitionPackages() {
  const auto message = makePackage();
  const auto header = mcpd4::makePartitionPackageTransferHeader(message);
  mcpd4::PartitionPackageAssembler incomplete(header, {});
  requireThrows([&] { (void)incomplete.finish(); },
                "incomplete multipart package should fail");

  mcpd4::PartitionPackageAssembler wrong_id(header, {});
  auto chunk = mcpd4::decodePartitionPackageTransferChunk(
      mcpd4::encodePartitionPackageTransferChunk(
          message, mcpd4::PartitionPackageSection::ARCS, 0, 1));
  ++chunk.partition_id;
  requireThrows([&] { wrong_id.append(std::move(chunk)); },
                "multipart chunk partition mismatch should fail");

  mcpd4::PartitionPackageAssembler wrong_offset(header, {});
  auto later_chunk = mcpd4::decodePartitionPackageTransferChunk(
      mcpd4::encodePartitionPackageTransferChunk(
          message, mcpd4::PartitionPackageSection::ARCS, 1, 1));
  requireThrows([&] { wrong_offset.append(std::move(later_chunk)); },
                "out-of-order multipart chunk should fail");

  mcpd4::PartitionPackageAssembler wrong_type(header, {});
  mcpd4::PartitionPackageTransferChunk typed_wrong;
  typed_wrong.partition_id = message.partition_id;
  typed_wrong.section = mcpd4::PartitionPackageSection::ARCS;
  typed_wrong.capacity_values = {1};
  requireThrows([&] { wrong_type.append(std::move(typed_wrong)); },
                "multipart chunk value type mismatch should fail");

  mcpd4::PartitionPackageAssembler empty_chunk_assembler(header, {});
  mcpd4::PartitionPackageTransferChunk empty_chunk;
  empty_chunk.partition_id = message.partition_id;
  empty_chunk.section = mcpd4::PartitionPackageSection::ARCS;
  requireThrows(
      [&] { empty_chunk_assembler.append(std::move(empty_chunk)); },
      "empty multipart chunk should fail");

  requireThrows(
      [&] {
        (void)mcpd4::encodePartitionPackageTransferChunk(
            message, mcpd4::PartitionPackageSection::ARCS,
            message.arcs.size(), 1);
      },
      "multipart chunk outside section should fail");

  auto unknown_section = mcpd4::encodePartitionPackageTransferChunk(
      message, mcpd4::PartitionPackageSection::ARCS, 0, 1);
  unknown_section[16] = 99;
  requireThrows(
      [&] { mcpd4::decodePartitionPackageTransferChunk(unknown_section); },
      "unknown multipart section should fail");

  auto truncated_begin = mcpd4::encodePartitionPackageTransferBegin(header);
  truncated_begin.pop_back();
  requireThrows(
      [&] { mcpd4::decodePartitionPackageTransferBegin(truncated_begin); },
      "truncated multipart begin should fail");
  auto truncated_chunk = mcpd4::encodePartitionPackageTransferChunk(
      message, mcpd4::PartitionPackageSection::ARCS, 0, 1);
  truncated_chunk.pop_back();
  requireThrows(
      [&] { mcpd4::decodePartitionPackageTransferChunk(truncated_chunk); },
      "truncated multipart chunk should fail");
  auto truncated_end = mcpd4::encodePartitionPackageTransferEnd(
      mcpd4::PartitionPackageTransferEnd{message.partition_id});
  truncated_end.pop_back();
  requireThrows(
      [&] { mcpd4::decodePartitionPackageTransferEnd(truncated_end); },
      "truncated multipart end should fail");

  auto invalid_header = header;
  invalid_header.partition_id = -1;
  requireThrows(
      [&] { mcpd4::PartitionPackageAssembler assembler(invalid_header, {}); },
      "negative multipart partition id should fail");
  invalid_header = header;
  invalid_header.local_node_count = -1;
  requireThrows(
      [&] { mcpd4::PartitionPackageAssembler assembler(invalid_header, {}); },
      "negative multipart node count should fail");
  invalid_header = header;
  invalid_header.objective_multiplier = 0;
  requireThrows(
      [&] { mcpd4::PartitionPackageAssembler assembler(invalid_header, {}); },
      "non-positive multipart objective multiplier should fail");
  invalid_header = header;
  invalid_header.reference_cut_check_interval = 0;
  requireThrows(
      [&] { mcpd4::PartitionPackageAssembler assembler(invalid_header, {}); },
      "non-positive multipart reference interval should fail");
  invalid_header = header;
  ++invalid_header.section_counts[static_cast<std::size_t>(
      mcpd4::PartitionPackageSection::ARCS)];
  requireThrows(
      [&] { mcpd4::PartitionPackageAssembler assembler(invalid_header, {}); },
      "odd multipart arc count should fail");
  invalid_header = header;
  --invalid_header.section_counts[static_cast<std::size_t>(
      mcpd4::PartitionPackageSection::ARC_CAPACITIES)];
  requireThrows(
      [&] { mcpd4::PartitionPackageAssembler assembler(invalid_header, {}); },
      "multipart arc capacity mismatch should fail");
  invalid_header = header;
  --invalid_header.section_counts[static_cast<std::size_t>(
      mcpd4::PartitionPackageSection::TERMINAL_CAPACITIES)];
  requireThrows(
      [&] { mcpd4::PartitionPackageAssembler assembler(invalid_header, {}); },
      "multipart terminal capacity mismatch should fail");
  invalid_header = header;
  --invalid_header.section_counts[static_cast<std::size_t>(
      mcpd4::PartitionPackageSection::LOCAL_TO_GLOBAL)];
  requireThrows(
      [&] { mcpd4::PartitionPackageAssembler assembler(invalid_header, {}); },
      "multipart local-to-global mismatch should fail");
  invalid_header = header;
  --invalid_header.section_counts[static_cast<std::size_t>(
      mcpd4::PartitionPackageSection::REFERENCE_CUT_LABELS)];
  requireThrows(
      [&] { mcpd4::PartitionPackageAssembler assembler(invalid_header, {}); },
      "multipart reference label mismatch should fail");
}

void roundTripsBoundedFullLabels() {
  const mcpd4::FullLabelsRequest request{/*partition_id=*/7,
                                         /*offset=*/1,
                                         /*count=*/3};
  const auto decoded_request = mcpd4::decodeFullLabelsRequest(
      mcpd4::encodeFullLabelsRequest(request));
  require(decoded_request.partition_id == request.partition_id &&
              decoded_request.offset == request.offset &&
              decoded_request.count == request.count,
          "full label request mismatch");

  mcpd4::FullLabelsChunk chunk;
  chunk.partition_id = 7;
  chunk.offset = 1;
  chunk.labels = {
      mcpd3::NodeLabel{/*global_node_id=*/20, /*local_index=*/1, /*label=*/0},
      mcpd3::NodeLabel{/*global_node_id=*/30, /*local_index=*/2, /*label=*/1}};
  const auto decoded_chunk = mcpd4::decodeFullLabelsChunk(
      mcpd4::encodeFullLabelsChunk(chunk));
  require(decoded_chunk.partition_id == chunk.partition_id &&
              decoded_chunk.offset == chunk.offset &&
              decoded_chunk.labels.size() == chunk.labels.size(),
          "full label chunk metadata mismatch");
  for (std::size_t i = 0; i < chunk.labels.size(); ++i) {
    requireNodeLabelEqual(decoded_chunk.labels[i], chunk.labels[i]);
  }

  const mcpd4::FullLabelsEnd end{/*partition_id=*/7};
  require(mcpd4::decodeFullLabelsEnd(mcpd4::encodeFullLabelsEnd(end))
              .partition_id == end.partition_id,
          "full label end mismatch");
}

void rejectsMalformedBoundedFullLabels() {
  requireThrows(
      [] {
        (void)mcpd4::decodeFullLabelsRequest(mcpd4::encodeFullLabelsRequest(
            mcpd4::FullLabelsRequest{/*partition_id=*/-1,
                                     /*offset=*/0,
                                     /*count=*/0}));
      },
      "negative full label request partition should fail");
  requireThrows(
      [] {
        (void)mcpd4::encodeFullLabelsChunk(mcpd4::FullLabelsChunk{});
      },
      "empty full label chunk should fail");
  mcpd4::FullLabelsChunk non_binary;
  non_binary.partition_id = 1;
  non_binary.labels.push_back(
      mcpd3::NodeLabel{/*global_node_id=*/1, /*local_index=*/0, /*label=*/2});
  requireThrows([&] { (void)mcpd4::encodeFullLabelsChunk(non_binary); },
                "non-binary full label chunk should fail encoding");

  mcpd4::FullLabelsChunk valid;
  valid.partition_id = 1;
  valid.labels.push_back(
      mcpd3::NodeLabel{/*global_node_id=*/1, /*local_index=*/0, /*label=*/1});
  auto malformed_label = mcpd4::encodeFullLabelsChunk(valid);
  malformed_label[36] = 2;
  requireThrows([&] { (void)mcpd4::decodeFullLabelsChunk(malformed_label); },
                "non-binary full label chunk should fail decoding");

  auto truncated = mcpd4::encodeFullLabelsChunk(valid);
  truncated.pop_back();
  requireThrows([&] { (void)mcpd4::decodeFullLabelsChunk(truncated); },
                "truncated full label chunk should fail");
  requireThrows(
      [] {
        (void)mcpd4::decodeFullLabelsEnd(mcpd4::encodeFullLabelsEnd(
            mcpd4::FullLabelsEnd{/*partition_id=*/-1}));
      },
      "negative full label end partition should fail");
}

void roundTripsReady() {
  mcpd4::ReadyMessage message;
  message.worker_name = "worker-ready";
  const auto decoded = mcpd4::decodeReady(
      mcpd4::encodeReady(message));
  require(decoded.worker_name == message.worker_name, "ready mismatch");
}

void roundTripsSolveRoundRequest() {
  mcpd3::PartitionSolveRequest message;
  message.round_id = 123;
  message.partition_id = 9;
  message.scale = 1000;
  message.regularization_strength = 10;
  message.return_full_labels = true;
  message.alpha_updates.push_back(
      mcpd3::AlphaUpdate{/*constraint_id=*/1,
                          /*alpha=*/-2,
                          /*last_alpha=*/3,
                          /*alpha_momentum=*/0.75f});
  message.alpha_updates.push_back(
      mcpd3::AlphaUpdate{/*constraint_id=*/4,
                          /*alpha=*/5,
                          /*last_alpha=*/-6,
                          /*alpha_momentum=*/-1.5f});

  const auto encoded = mcpd4::encodeSolveRoundRequest(message);
  require(!encoded.empty(), "solve request encoding should not be empty");
  const auto decoded = mcpd4::decodeSolveRoundRequest(encoded);
  require(decoded.round_id == message.round_id, "request round mismatch");
  require(decoded.partition_id == message.partition_id,
          "request partition mismatch");
  require(decoded.scale == message.scale, "request scale mismatch");
  require(decoded.regularization_strength == message.regularization_strength,
          "request regularization mismatch");
  require(decoded.return_full_labels == message.return_full_labels,
          "request full-label flag mismatch");
  require(decoded.alpha_updates.size() == message.alpha_updates.size(),
          "request alpha count mismatch");
  for (size_t i = 0; i < message.alpha_updates.size(); ++i) {
    requireCompactAlphaUpdateEqual(decoded.alpha_updates[i],
                                   message.alpha_updates[i]);
  }
}

void roundTripsSolveRoundBatchRequest() {
  mcpd3::PartitionSolveRequest first;
  first.round_id = 123;
  first.partition_id = 9;
  first.scale = 1000;
  first.regularization_strength = 10;
  first.alpha_updates.push_back(
      mcpd3::AlphaUpdate{/*constraint_id=*/1,
                          /*alpha=*/-2,
                          /*last_alpha=*/3,
                          /*alpha_momentum=*/0.75f});

  mcpd3::PartitionSolveRequest second;
  second.round_id = 124;
  second.partition_id = 10;
  second.scale = 100;
  second.regularization_strength = 0;
  second.return_full_labels = true;
  second.alpha_updates.push_back(
      mcpd3::AlphaUpdate{/*constraint_id=*/4,
                          /*alpha=*/5,
                          /*last_alpha=*/-6,
                          /*alpha_momentum=*/-1.5f});

  const std::vector<mcpd3::PartitionSolveRequest> messages{first, second};
  const auto encoded = mcpd4::encodeSolveRoundBatchRequest(messages);
  require(!encoded.empty(), "batch request encoding should not be empty");
  const auto decoded = mcpd4::decodeSolveRoundBatchRequest(encoded);

  require(decoded.size() == messages.size(),
          "batch request count mismatch");
  for (size_t i = 0; i < messages.size(); ++i) {
    require(decoded[i].round_id == messages[i].round_id,
            "batch request round mismatch");
    require(decoded[i].partition_id == messages[i].partition_id,
            "batch request partition mismatch");
    require(decoded[i].scale == messages[i].scale,
            "batch request scale mismatch");
    require(decoded[i].regularization_strength ==
                messages[i].regularization_strength,
            "batch request regularization mismatch");
    require(decoded[i].return_full_labels == messages[i].return_full_labels,
            "batch request full-label flag mismatch");
    require(decoded[i].alpha_updates.size() ==
                messages[i].alpha_updates.size(),
            "batch request alpha count mismatch");
    for (size_t j = 0; j < messages[i].alpha_updates.size(); ++j) {
      requireCompactAlphaUpdateEqual(decoded[i].alpha_updates[j],
                                     messages[i].alpha_updates[j]);
    }
  }
}

void roundTripsSolveRoundResult() {
  mcpd3::PartitionSolveResult message;
  message.round_id = 77;
  message.partition_id = 8;
  message.lower_bound = -100;
  message.regularization_budget = 11;
  message.regularization_contribution = 7;
  message.regularization_anchor_sink_count = 5;
  message.regularization_active_sink_count = 3;
  message.constrained_labels.push_back(
      mcpd3::ConstraintLabel{/*constraint_id=*/10,
                              /*global_node_id=*/20,
                              /*local_index=*/1,
                              /*label=*/0});
  message.constrained_labels.push_back(
      mcpd3::ConstraintLabel{/*constraint_id=*/11,
                              /*global_node_id=*/21,
                              /*local_index=*/2,
                              /*label=*/1});
  message.full_labels.push_back(
      mcpd3::NodeLabel{/*global_node_id=*/100,
                        /*local_index=*/0,
                        /*label=*/1});
  message.full_labels.push_back(
      mcpd3::NodeLabel{/*global_node_id=*/101,
                        /*local_index=*/1,
                        /*label=*/0});

  const auto encoded = mcpd4::encodeSolveRoundResult(message);
  require(!encoded.empty(), "solve result encoding should not be empty");
  const auto decoded = mcpd4::decodeSolveRoundResult(encoded);
  require(decoded.round_id == message.round_id, "result round mismatch");
  require(decoded.partition_id == message.partition_id,
          "result partition mismatch");
  require(decoded.lower_bound == message.lower_bound,
          "result lower bound mismatch");
  require(decoded.regularization_budget == message.regularization_budget,
          "result budget mismatch");
  require(decoded.regularization_contribution ==
              message.regularization_contribution,
          "result contribution mismatch");
  require(decoded.regularization_anchor_sink_count ==
              message.regularization_anchor_sink_count,
          "result anchor count mismatch");
  require(decoded.regularization_active_sink_count ==
              message.regularization_active_sink_count,
          "result active count mismatch");
  require(decoded.constrained_labels.size() ==
              message.constrained_labels.size(),
          "result label count mismatch");
  for (size_t i = 0; i < message.constrained_labels.size(); ++i) {
    requireCompactLabelEqual(decoded.constrained_labels[i],
                             message.constrained_labels[i]);
  }
  require(decoded.full_labels.size() == message.full_labels.size(),
          "result full label count mismatch");
  for (size_t i = 0; i < message.full_labels.size(); ++i) {
    requireNodeLabelEqual(decoded.full_labels[i], message.full_labels[i]);
  }

  const auto decoded_default_timing =
      mcpd4::decodeTimedSolveRoundResult(
          mcpd4::encodeSolveRoundResult(message));
  require(decoded_default_timing.worker_solve_wall_us == 0,
          "default result timing should be zero");

  const auto decoded_timed = mcpd4::decodeTimedSolveRoundResult(
      mcpd4::encodeSolveRoundResultWithTiming(
          message, /*worker_solve_wall_us=*/12345));
  require(decoded_timed.result.round_id == message.round_id,
          "timed result round mismatch");
  require(decoded_timed.worker_solve_wall_us == 12345,
          "timed result worker solve timing mismatch");
}

void roundTripsSolveRoundBatchResult() {
  mcpd3::PartitionSolveResult first;
  first.round_id = 77;
  first.partition_id = 8;
  first.lower_bound = -100;
  first.regularization_budget = 11;
  first.regularization_contribution = 7;
  first.regularization_anchor_sink_count = 5;
  first.regularization_active_sink_count = 3;
  first.constrained_labels.push_back(
      mcpd3::ConstraintLabel{/*constraint_id=*/10,
                              /*global_node_id=*/20,
                              /*local_index=*/1,
                              /*label=*/0});
  first.full_labels.push_back(
      mcpd3::NodeLabel{/*global_node_id=*/200,
                        /*local_index=*/0,
                        /*label=*/0});

  mcpd3::PartitionSolveResult second;
  second.round_id = 78;
  second.partition_id = 9;
  second.lower_bound = 200;
  second.regularization_budget = 13;
  second.regularization_contribution = 2;
  second.regularization_anchor_sink_count = 1;
  second.regularization_active_sink_count = 1;
  second.constrained_labels.push_back(
      mcpd3::ConstraintLabel{/*constraint_id=*/11,
                              /*global_node_id=*/21,
                              /*local_index=*/2,
                              /*label=*/1});
  second.full_labels.push_back(
      mcpd3::NodeLabel{/*global_node_id=*/201,
                        /*local_index=*/0,
                        /*label=*/1});

  const std::vector<mcpd3::PartitionSolveResult> messages{first, second};
  const auto encoded = mcpd4::encodeSolveRoundBatchResult(messages);
  require(!encoded.empty(), "batch result encoding should not be empty");
  const auto decoded = mcpd4::decodeSolveRoundBatchResult(encoded);
  require(decoded.size() == messages.size(), "batch result count mismatch");
  for (size_t i = 0; i < messages.size(); ++i) {
    require(decoded[i].round_id == messages[i].round_id,
            "batch result round mismatch");
    require(decoded[i].partition_id == messages[i].partition_id,
            "batch result partition mismatch");
    require(decoded[i].lower_bound == messages[i].lower_bound,
            "batch result lower bound mismatch");
    require(decoded[i].regularization_budget ==
                messages[i].regularization_budget,
            "batch result budget mismatch");
    require(decoded[i].regularization_contribution ==
                messages[i].regularization_contribution,
            "batch result contribution mismatch");
    require(decoded[i].regularization_anchor_sink_count ==
                messages[i].regularization_anchor_sink_count,
            "batch result anchor count mismatch");
    require(decoded[i].regularization_active_sink_count ==
                messages[i].regularization_active_sink_count,
            "batch result active count mismatch");
    require(decoded[i].constrained_labels.size() ==
                messages[i].constrained_labels.size(),
            "batch result label count mismatch");
    for (size_t j = 0; j < messages[i].constrained_labels.size(); ++j) {
      requireCompactLabelEqual(decoded[i].constrained_labels[j],
                               messages[i].constrained_labels[j]);
    }
    require(decoded[i].full_labels.size() == messages[i].full_labels.size(),
            "batch result full label count mismatch");
    for (size_t j = 0; j < messages[i].full_labels.size(); ++j) {
      requireNodeLabelEqual(decoded[i].full_labels[j],
                            messages[i].full_labels[j]);
    }
  }

  const auto decoded_default_timing =
      mcpd4::decodeTimedSolveRoundBatchResult(
          mcpd4::encodeSolveRoundBatchResult(messages));
  require(decoded_default_timing.worker_solve_wall_us == 0,
          "default batch timing should be zero");

  const auto decoded_timed =
      mcpd4::decodeTimedSolveRoundBatchResult(
          mcpd4::encodeSolveRoundBatchResultWithTiming(
              messages, /*worker_solve_wall_us=*/56789));
  require(decoded_timed.results.size() == messages.size(),
          "timed batch result count mismatch");
  require(decoded_timed.results[1].partition_id == second.partition_id,
          "timed batch result payload mismatch");
  require(decoded_timed.worker_solve_wall_us == 56789,
          "timed batch worker solve timing mismatch");
}

void deltaSolveRoundRequestTracksTemporalAlphaState() {
  mcpd4::TemporalSolveCodecState encoder;
  mcpd4::TemporalSolveCodecState decoder;

  mcpd3::PartitionSolveRequest first;
  first.round_id = 1;
  first.partition_id = 4;
  first.scale = 1000;
  first.regularization_strength = 10;
  first.return_full_labels = true;
  first.alpha_updates.push_back(mcpd3::AlphaUpdate{
      /*constraint_id=*/10, /*alpha=*/100, /*last_alpha=*/0,
      /*alpha_momentum=*/0});
  first.alpha_updates.push_back(mcpd3::AlphaUpdate{
      /*constraint_id=*/11, /*alpha=*/-5, /*last_alpha=*/0,
      /*alpha_momentum=*/0});

  const auto first_encoded =
      mcpd4::encodeDeltaSolveRoundRequest(first, &encoder);
  const auto first_decoded =
      mcpd4::decodeDeltaSolveRoundRequest(first_encoded, &decoder);
  require(first_decoded.alpha_updates.size() == 2,
          "first delta request should fully sync alpha values");
  require(first_decoded.return_full_labels == first.return_full_labels,
          "first delta request should preserve the full-label flag");
  requireCompactAlphaUpdateEqual(first_decoded.alpha_updates[0],
                                 first.alpha_updates[0]);
  requireCompactAlphaUpdateEqual(first_decoded.alpha_updates[1],
                                 first.alpha_updates[1]);

  auto unchanged = first;
  unchanged.round_id = 2;
  const auto unchanged_encoded =
      mcpd4::encodeDeltaSolveRoundRequest(unchanged, &encoder);
  const auto unchanged_decoded =
      mcpd4::decodeDeltaSolveRoundRequest(unchanged_encoded, &decoder);
  require(unchanged_decoded.alpha_updates.empty(),
          "unchanged alpha values should be omitted from delta request");
  require(unchanged_decoded.return_full_labels == unchanged.return_full_labels,
          "unchanged delta request should preserve the full-label flag");
  require(unchanged_encoded.size() < first_encoded.size(),
          "unchanged delta request should be smaller than first sync");

  auto changed = first;
  changed.round_id = 3;
  changed.alpha_updates[0].alpha = 103;
  changed.alpha_updates[1].alpha = -9;
  const auto changed_encoded =
      mcpd4::encodeDeltaSolveRoundRequest(changed, &encoder);
  const auto changed_decoded =
      mcpd4::decodeDeltaSolveRoundRequest(changed_encoded, &decoder);
  require(changed_decoded.alpha_updates.size() == 2,
          "changed alpha values should be transmitted");
  requireCompactAlphaUpdateEqual(changed_decoded.alpha_updates[0],
                                 changed.alpha_updates[0]);
  requireCompactAlphaUpdateEqual(changed_decoded.alpha_updates[1],
                                 changed.alpha_updates[1]);
  require(changed_encoded.size() <
              mcpd4::encodeSolveRoundRequest(changed).size(),
          "small temporal alpha deltas should beat stateless alpha encoding");

  encoder.resetPartition(first.partition_id);
  decoder.resetPartition(first.partition_id);
  auto after_reset = changed;
  after_reset.round_id = 4;
  const auto reset_decoded = mcpd4::decodeDeltaSolveRoundRequest(
      mcpd4::encodeDeltaSolveRoundRequest(after_reset, &encoder), &decoder);
  require(reset_decoded.alpha_updates.size() == 2,
          "reset partition state should force a fresh alpha sync");
  requireCompactAlphaUpdateEqual(reset_decoded.alpha_updates[0],
                                 after_reset.alpha_updates[0]);
  requireCompactAlphaUpdateEqual(reset_decoded.alpha_updates[1],
                                 after_reset.alpha_updates[1]);
}

void deltaSolveRoundBatchRequestKeepsPartitionStateSeparate() {
  mcpd4::TemporalSolveCodecState encoder;
  mcpd4::TemporalSolveCodecState decoder;

  mcpd3::PartitionSolveRequest first;
  first.round_id = 1;
  first.partition_id = 0;
  first.scale = 100;
  first.alpha_updates.push_back(mcpd3::AlphaUpdate{
      /*constraint_id=*/7, /*alpha=*/10, /*last_alpha=*/0,
      /*alpha_momentum=*/0});

  auto second = first;
  second.partition_id = 1;
  second.alpha_updates[0].alpha = -10;

  const std::vector<mcpd3::PartitionSolveRequest> batch{first, second};
  const auto decoded_first = mcpd4::decodeDeltaSolveRoundBatchRequest(
      mcpd4::encodeDeltaSolveRoundBatchRequest(batch, &encoder), &decoder);
  require(decoded_first.size() == 2,
          "delta batch request should preserve request count");
  require(decoded_first[0].alpha_updates[0].alpha == 10,
          "first partition alpha should decode from its own state");
  require(decoded_first[1].alpha_updates[0].alpha == -10,
          "second partition alpha should decode from its own state");

  auto changed_second = second;
  changed_second.round_id = 2;
  changed_second.alpha_updates[0].alpha = -7;
  const std::vector<mcpd3::PartitionSolveRequest> changed_batch{
      first, changed_second};
  const auto changed_encoded =
      mcpd4::encodeDeltaSolveRoundBatchRequest(changed_batch, &encoder);
  const auto changed_decoded =
      mcpd4::decodeDeltaSolveRoundBatchRequest(changed_encoded, &decoder);
  require(changed_decoded[0].alpha_updates.empty(),
          "unchanged partition in batch should not resend alpha");
  require(changed_decoded[1].alpha_updates.size() == 1,
          "changed partition in batch should send only its alpha delta");
  require(changed_decoded[1].alpha_updates[0].alpha == -7,
          "changed partition alpha should reconstruct from its own baseline");
}

void deltaSolveRoundResultReconstructsFullLabels() {
  mcpd4::TemporalSolveCodecState encoder;
  mcpd4::TemporalSolveCodecState decoder;

  mcpd3::PartitionSolveResult first;
  first.round_id = 10;
  first.partition_id = 3;
  first.lower_bound = 99;
  first.regularization_budget = 7;
  first.regularization_contribution = 5;
  first.regularization_anchor_sink_count = 2;
  first.regularization_active_sink_count = 1;
  first.constrained_labels.push_back(
      mcpd3::ConstraintLabel{/*constraint_id=*/20,
                              /*global_node_id=*/100,
                              /*local_index=*/0,
                              /*label=*/0});
  first.constrained_labels.push_back(
      mcpd3::ConstraintLabel{/*constraint_id=*/21,
                              /*global_node_id=*/101,
                              /*local_index=*/1,
                              /*label=*/1});
  first.full_labels.push_back(
      mcpd3::NodeLabel{/*global_node_id=*/300,
                        /*local_index=*/0,
                        /*label=*/0});
  first.full_labels.push_back(
      mcpd3::NodeLabel{/*global_node_id=*/301,
                        /*local_index=*/1,
                        /*label=*/1});

  const auto first_encoded =
      mcpd4::encodeDeltaSolveRoundResultWithTiming(
          first, /*worker_solve_wall_us=*/42, &encoder);
  const auto first_decoded =
      mcpd4::decodeDeltaTimedSolveRoundResult(first_encoded, &decoder);
  require(first_decoded.worker_solve_wall_us == 42,
          "delta result should preserve worker timing");
  require(first_decoded.result.constrained_labels.size() == 2,
          "first delta result should fully sync labels");
  requireCompactLabelEqual(first_decoded.result.constrained_labels[0],
                           first.constrained_labels[0]);
  requireCompactLabelEqual(first_decoded.result.constrained_labels[1],
                           first.constrained_labels[1]);
  require(first_decoded.result.full_labels.size() == 2,
          "first delta result should preserve requested full labels");
  requireNodeLabelEqual(first_decoded.result.full_labels[0],
                        first.full_labels[0]);
  requireNodeLabelEqual(first_decoded.result.full_labels[1],
                        first.full_labels[1]);

  auto unchanged = first;
  unchanged.round_id = 11;
  unchanged.lower_bound = 100;
  const auto unchanged_encoded =
      mcpd4::encodeDeltaSolveRoundResultWithTiming(
          unchanged, /*worker_solve_wall_us=*/43, &encoder);
  const auto unchanged_decoded =
      mcpd4::decodeDeltaTimedSolveRoundResult(unchanged_encoded, &decoder);
  require(unchanged_decoded.result.constrained_labels.size() == 2,
          "unchanged label delta should reconstruct full label list");
  requireCompactLabelEqual(unchanged_decoded.result.constrained_labels[0],
                           unchanged.constrained_labels[0]);
  requireCompactLabelEqual(unchanged_decoded.result.constrained_labels[1],
                           unchanged.constrained_labels[1]);
  require(unchanged_decoded.result.full_labels.size() == 2,
          "unchanged label delta should still carry full labels when present");
  requireNodeLabelEqual(unchanged_decoded.result.full_labels[0],
                        unchanged.full_labels[0]);
  requireNodeLabelEqual(unchanged_decoded.result.full_labels[1],
                        unchanged.full_labels[1]);
  require(unchanged_encoded.size() < first_encoded.size(),
          "unchanged delta result should be smaller than first sync");

  auto changed = first;
  changed.round_id = 12;
  changed.constrained_labels[0].label = 1;
  const auto changed_decoded = mcpd4::decodeDeltaTimedSolveRoundResult(
      mcpd4::encodeDeltaSolveRoundResultWithTiming(
          changed, /*worker_solve_wall_us=*/44, &encoder),
      &decoder);
  require(changed_decoded.result.constrained_labels.size() == 2,
          "changed label delta should reconstruct full label list");
  require(changed_decoded.result.constrained_labels[0].label == 1,
          "changed label value should be applied to reconstructed result");
  require(changed_decoded.result.constrained_labels[1].label == 1,
          "unchanged label value should remain in reconstructed result");

  auto replaced_id = changed;
  replaced_id.round_id = 13;
  replaced_id.constrained_labels[0].constraint_id = 22;
  replaced_id.constrained_labels[0].label = 0;
  const auto replaced_decoded = mcpd4::decodeDeltaTimedSolveRoundResult(
      mcpd4::encodeDeltaSolveRoundResultWithTiming(
          replaced_id, /*worker_solve_wall_us=*/45, &encoder),
      &decoder);
  require(replaced_decoded.result.constrained_labels.size() == 2,
          "same-size label id replacement should full sync exact label set");
  require(replaced_decoded.result.constrained_labels[0].constraint_id == 22,
          "label id replacement should not keep stale first constraint id");
  require(replaced_decoded.result.constrained_labels[1].constraint_id == 21,
          "label id replacement should preserve remaining constraint id");

  mcpd4::TemporalSolveCodecState stale_decoder;
  requireThrows(
      [&] {
        (void)mcpd4::decodeDeltaTimedSolveRoundResult(unchanged_encoded,
                                                      &stale_decoder);
      },
      "label deltas should require an earlier full label sync");
}

void deltaSolveRoundBatchResultShrinksRepeatedLabels() {
  mcpd4::TemporalSolveCodecState encoder;
  mcpd4::TemporalSolveCodecState decoder;

  mcpd3::PartitionSolveResult result;
  result.round_id = 20;
  result.partition_id = 6;
  result.lower_bound = 50;
  for (int i = 0; i < 128; ++i) {
    result.constrained_labels.push_back(
        mcpd3::ConstraintLabel{/*constraint_id=*/1000 + i,
                                /*global_node_id=*/2000 + i,
                                /*local_index=*/i,
                                /*label=*/i % 2});
  }

  const std::vector<mcpd3::PartitionSolveResult> first_batch{result};
  const auto first_encoded =
      mcpd4::encodeDeltaSolveRoundBatchResultWithTiming(
          first_batch, /*worker_solve_wall_us=*/10, &encoder);
  const auto first_decoded =
      mcpd4::decodeDeltaTimedSolveRoundBatchResult(first_encoded, &decoder);
  require(first_decoded.results.size() == 1,
          "delta batch result should preserve result count");
  require(first_decoded.results[0].constrained_labels.size() == 128,
          "first batch result should sync all labels");

  result.round_id = 21;
  result.lower_bound = 51;
  const std::vector<mcpd3::PartitionSolveResult> repeated_batch{result};
  const auto repeated_encoded =
      mcpd4::encodeDeltaSolveRoundBatchResultWithTiming(
          repeated_batch, /*worker_solve_wall_us=*/11, &encoder);
  const auto repeated_decoded =
      mcpd4::decodeDeltaTimedSolveRoundBatchResult(repeated_encoded,
                                                   &decoder);
  require(repeated_decoded.results[0].constrained_labels.size() == 128,
          "repeated batch delta should reconstruct all labels");
  require(repeated_encoded.size() * 2 < first_encoded.size(),
          "repeated label batch should be less than half the first sync size");
}

void roundTripsScaleObjective() {
  mcpd4::ScaleObjectiveMessage message;
  message.factor = 10;
  message.saturate_capacity_overflow = true;
  const auto decoded = mcpd4::decodeScaleObjective(
      mcpd4::encodeScaleObjective(message));
  require(decoded.factor == message.factor, "scale objective factor mismatch");
  require(decoded.saturate_capacity_overflow ==
              message.saturate_capacity_overflow,
          "scale objective saturation flag mismatch");
}

void roundTripsPartitionCapacityUpdate() {
  mcpd3::PartitionCapacityUpdate update;
  update.partition_id = 17;
  update.arc_capacities = {3, 4, 5, 6};
  update.terminal_capacities = {-7, 8, 0};
  update.preserve_flow_state = false;
  update.flow_scale_numerator = 13;
  update.flow_scale_denominator = 9;

  const auto decoded = mcpd4::decodePartitionCapacityUpdate(
      mcpd4::encodePartitionCapacityUpdate(update));
  require(decoded.partition_id == update.partition_id,
          "capacity update partition mismatch");
  require(decoded.arc_capacities == update.arc_capacities,
          "capacity update arc capacities mismatch");
  require(decoded.terminal_capacities == update.terminal_capacities,
          "capacity update terminal capacities mismatch");
  require(decoded.preserve_flow_state == update.preserve_flow_state,
          "capacity update preserve-flow flag mismatch");
  require(decoded.flow_scale_numerator == update.flow_scale_numerator,
          "capacity update flow numerator mismatch");
  require(decoded.flow_scale_denominator == update.flow_scale_denominator,
          "capacity update flow denominator mismatch");
}

void roundTripsAlphaUpdate() {
  mcpd4::AlphaUpdateMessage message;
  message.partition_id = 5;
  message.alpha_updates.push_back(
      mcpd3::AlphaUpdate{/*constraint_id=*/6,
                          /*alpha=*/7,
                          /*last_alpha=*/8,
                          /*alpha_momentum=*/2.25f});
  const auto encoded = mcpd4::encodeAlphaUpdate(message);
  require(!encoded.empty(), "standalone alpha encoding should not be empty");
  const auto decoded = mcpd4::decodeAlphaUpdate(encoded);
  require(decoded.partition_id == message.partition_id,
          "alpha message partition mismatch");
  require(decoded.alpha_updates.size() == 1, "alpha message count mismatch");
  requireCompactAlphaUpdateEqual(decoded.alpha_updates[0],
                                 message.alpha_updates[0]);
}

void roundTripsStop() {
  mcpd4::StopMessage message;
  message.reason = 12;
  message.message = "done";
  const auto decoded = mcpd4::decodeStop(
      mcpd4::encodeStop(message));
  require(decoded.reason == message.reason, "stop reason mismatch");
  require(decoded.message == message.message, "stop message mismatch");
}

void roundTripsError() {
  mcpd4::ErrorMessage message;
  message.code = 99;
  message.message = "bad frame";
  const auto decoded = mcpd4::decodeError(
      mcpd4::encodeError(message));
  require(decoded.code == message.code, "error code mismatch");
  require(decoded.message == message.message, "error message mismatch");
}

void roundTripsLinearWorkerMessages() {
  mcpd4::LinearStructureMessage structure;
  structure.partition_id = 7;
  structure.owned_global_nodes = {10, 11};
  structure.ghost_global_nodes = {20};
  structure.row_offsets = {0, 2, 5};
  structure.column_indices = {0, 1, 0, 1, 2};
  structure.boundary_owned_local_indices = {1};
  const auto decoded_structure = mcpd4::decodeLinearStructure(
      mcpd4::encodeLinearStructure(structure));
  require(decoded_structure.partition_id == structure.partition_id,
          "linear structure partition mismatch");
  require(decoded_structure.owned_global_nodes == structure.owned_global_nodes,
          "linear structure owned nodes mismatch");
  require(decoded_structure.ghost_global_nodes == structure.ghost_global_nodes,
          "linear structure ghost nodes mismatch");
  require(decoded_structure.row_offsets == structure.row_offsets,
          "linear structure row offsets mismatch");
  require(decoded_structure.column_indices == structure.column_indices,
          "linear structure columns mismatch");
  require(decoded_structure.boundary_owned_local_indices ==
              structure.boundary_owned_local_indices,
          "linear structure boundary indices mismatch");

  mcpd4::LinearSystemValuesMessage system;
  system.partition_id = 7;
  system.values = {4.0, -1.0, -1.0, 4.0, -1.0};
  system.rhs = {15.0, 10.0};
  system.initial_x = {0.25, -0.5};
  const auto decoded_system = mcpd4::decodeLinearSystemValues(
      mcpd4::encodeLinearSystemValues(system));
  require(decoded_system.partition_id == system.partition_id,
          "linear system partition mismatch");
  require(decoded_system.values == system.values,
          "linear system values mismatch");
  require(decoded_system.rhs == system.rhs, "linear system rhs mismatch");
  require(decoded_system.initial_x == system.initial_x,
          "linear system initial solution mismatch");

  mcpd4::LinearVectorRequest vector_request;
  vector_request.partition_id = 7;
  vector_request.values = {1.25, -2.5};
  const auto decoded_initialize = mcpd4::decodeLinearInitializeRequest(
      mcpd4::encodeLinearInitializeRequest(vector_request));
  require(decoded_initialize.partition_id == vector_request.partition_id &&
              decoded_initialize.values == vector_request.values,
          "linear initialize request mismatch");
  const auto decoded_multiply = mcpd4::decodeLinearMultiplyRequest(
      mcpd4::encodeLinearMultiplyRequest(vector_request));
  require(decoded_multiply.partition_id == vector_request.partition_id &&
              decoded_multiply.values == vector_request.values,
          "linear multiply request mismatch");

  mcpd4::LinearInitializeResultMessage initialize_result;
  initialize_result.partition_id = 7;
  initialize_result.result.rhs_norm_squared = 12.5;
  initialize_result.result.residual_norm_squared = 3.25;
  initialize_result.result.residual_preconditioned_inner = 2.75;
  initialize_result.result.boundary_direction = {-1.0, 2.0};
  const auto decoded_initialize_result =
      mcpd4::decodeLinearInitializeResult(
          mcpd4::encodeLinearInitializeResult(initialize_result));
  require(decoded_initialize_result.partition_id == 7 &&
              decoded_initialize_result.result.rhs_norm_squared == 12.5 &&
              decoded_initialize_result.result.residual_norm_squared == 3.25 &&
              decoded_initialize_result.result
                      .residual_preconditioned_inner == 2.75 &&
              decoded_initialize_result.result.boundary_direction ==
                  std::vector<double>({-1.0, 2.0}),
          "linear initialize result mismatch");

  mcpd4::LinearScalarRequest scalar_request;
  scalar_request.partition_id = 7;
  scalar_request.value = -0.125;
  const auto decoded_alpha = mcpd4::decodeLinearAlphaRequest(
      mcpd4::encodeLinearAlphaRequest(scalar_request));
  require(decoded_alpha.partition_id == 7 && decoded_alpha.value == -0.125,
          "linear alpha request mismatch");
  const auto decoded_beta = mcpd4::decodeLinearBetaRequest(
      mcpd4::encodeLinearBetaRequest(scalar_request));
  require(decoded_beta.partition_id == 7 && decoded_beta.value == -0.125,
          "linear beta request mismatch");

  mcpd4::LinearMultiplyResultMessage multiply_result;
  multiply_result.partition_id = 7;
  multiply_result.result.direction_product_inner = 91.0;
  const auto decoded_multiply_result = mcpd4::decodeLinearMultiplyResult(
      mcpd4::encodeLinearMultiplyResult(multiply_result));
  require(decoded_multiply_result.partition_id == 7 &&
              decoded_multiply_result.result.direction_product_inner == 91.0,
          "linear multiply result mismatch");

  mcpd4::LinearAlphaResultMessage alpha_result;
  alpha_result.partition_id = 7;
  alpha_result.result.residual_norm_squared = 4.5;
  alpha_result.result.residual_preconditioned_inner = 3.5;
  const auto decoded_alpha_result = mcpd4::decodeLinearAlphaResult(
      mcpd4::encodeLinearAlphaResult(alpha_result));
  require(decoded_alpha_result.partition_id == 7 &&
              decoded_alpha_result.result.residual_norm_squared == 4.5 &&
              decoded_alpha_result.result.residual_preconditioned_inner == 3.5,
          "linear alpha result mismatch");

  mcpd4::LinearBetaResultMessage beta_result;
  beta_result.partition_id = 7;
  beta_result.result.boundary_direction = {8.0, 9.0};
  const auto decoded_beta_result = mcpd4::decodeLinearBetaResult(
      mcpd4::encodeLinearBetaResult(beta_result));
  require(decoded_beta_result.partition_id == 7 &&
              decoded_beta_result.result.boundary_direction ==
                  std::vector<double>({8.0, 9.0}),
          "linear beta result mismatch");

  mcpd4::LinearPartitionRequest partition_request;
  partition_request.partition_id = 7;
  const auto decoded_solution_request = mcpd4::decodeLinearSolutionRequest(
      mcpd4::encodeLinearSolutionRequest(partition_request));
  require(decoded_solution_request.partition_id == 7,
          "linear solution request mismatch");
  mcpd4::LinearSolutionResultMessage solution_result;
  solution_result.partition_id = 7;
  solution_result.solution = {5.0, 5.0};
  const auto decoded_solution = mcpd4::decodeLinearSolutionResult(
      mcpd4::encodeLinearSolutionResult(solution_result));
  require(decoded_solution.partition_id == 7 &&
              decoded_solution.solution == solution_result.solution,
          "linear solution result mismatch");
}

void rejectsMalformedFrames() {
  requireThrows([] { mcpd4::decodeFrame({1, 0, 0}); },
                "truncated frame header should fail");

  auto unknown = mcpd4::encodeFrame(
      mcpd4::MessageType::HELLO, {});
  unknown[0] = 250;
  requireThrows([&] { mcpd4::decodeFrame(unknown); },
                "unknown message type should fail");

  auto size_mismatch = mcpd4::encodeFrame(
      mcpd4::MessageType::READY, {1, 2, 3});
  size_mismatch.pop_back();
  requireThrows([&] { mcpd4::decodeFrame(size_mismatch); },
                "payload size mismatch should fail");

  requireThrows(
      [] {
        mcpd4::decodeReady(
            mcpd4::encodeFrame(
                mcpd4::MessageType::ERROR, {}));
      },
      "wrong message type should fail");

  auto truncated_payload = mcpd4::encodeReady(
      mcpd4::ReadyMessage{"worker"});
  truncated_payload[12] = 200;
  requireThrows([&] { mcpd4::decodeReady(truncated_payload); },
                "truncated payload should fail");

  auto trailing_payload = mcpd4::encodeFrame(
      mcpd4::MessageType::SCALE_OBJECTIVE,
      std::vector<std::uint8_t>{1, 0, 0, 0, 0, 0, 0, 0, 99});
  requireThrows([&] {
    mcpd4::decodeScaleObjective(trailing_payload);
  }, "trailing payload bytes should fail");

  auto truncated_capacity_update = mcpd4::encodePartitionCapacityUpdate(
      mcpd3::PartitionCapacityUpdate{});
  truncated_capacity_update.pop_back();
  requireThrows(
      [&] { mcpd4::decodePartitionCapacityUpdate(truncated_capacity_update); },
      "truncated capacity update should fail");
}

} // namespace

int main() {
  try {
    frameHeaderIsLittleEndian();
    roundTripsHello();
    roundTripsConfiguredPrecisionExtremes();
    rejectsNonCanonicalAndOutOfRangeIntegers();
    roundTripsPartitionPackage();
    roundTripsMappedMultipartPartitionPackage();
    rejectsMalformedMultipartPartitionPackages();
    roundTripsBoundedFullLabels();
    rejectsMalformedBoundedFullLabels();
    roundTripsReady();
    roundTripsSolveRoundRequest();
    roundTripsSolveRoundBatchRequest();
    roundTripsSolveRoundResult();
    roundTripsSolveRoundBatchResult();
    deltaSolveRoundRequestTracksTemporalAlphaState();
    deltaSolveRoundBatchRequestKeepsPartitionStateSeparate();
    deltaSolveRoundResultReconstructsFullLabels();
    deltaSolveRoundBatchResultShrinksRepeatedLabels();
    roundTripsScaleObjective();
    roundTripsPartitionCapacityUpdate();
    roundTripsAlphaUpdate();
    roundTripsStop();
    roundTripsError();
    roundTripsLinearWorkerMessages();
    rejectsMalformedFrames();
  } catch (const std::exception &e) {
    std::cerr << "protocol_serialization_test failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
