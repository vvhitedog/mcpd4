#include <cstdlib>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include <mcpd3_distributed/protocol.h>

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
  } catch (const std::runtime_error &) {
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
  const auto frame = mcpd3_distributed::encodeFrame(
      mcpd3_distributed::MessageType::PARTITION_PACKAGE, payload);

  require(frame.size() == 14, "frame size mismatch");
  require(frame[0] == 2 && frame[1] == 0 && frame[2] == 0 && frame[3] == 0,
          "message type should be little-endian uint32");
  require(frame[4] == 2 && frame[5] == 0 && frame[6] == 0 && frame[7] == 0 &&
              frame[8] == 0 && frame[9] == 0 && frame[10] == 0 &&
              frame[11] == 0,
          "payload length should be little-endian uint64");

  const auto decoded = mcpd3_distributed::decodeFrame(frame);
  require(decoded.type == mcpd3_distributed::MessageType::PARTITION_PACKAGE,
          "decoded frame type mismatch");
  require(decoded.payload == payload, "decoded frame payload mismatch");
}

void roundTripsHello() {
  mcpd3_distributed::HelloMessage message;
  message.protocol_version = 3;
  message.worker_name = "worker-a";
  message.cpu_count = 16;
  message.ram_gb = 64;
  message.feature_bits = 0x1020;
  message.temp_path = "/tmp/mcpd3";
  message.debug_build = true;
  message.little_endian = true;

  const auto decoded = mcpd3_distributed::decodeHello(
      mcpd3_distributed::encodeHello(message));
  require(decoded.protocol_version == message.protocol_version,
          "hello protocol version mismatch");
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

void roundTripsPartitionPackage() {
  const auto message = makePackage();
  const auto decoded = mcpd3_distributed::decodePartitionPackage(
      mcpd3_distributed::encodePartitionPackage(message));
  requirePackageEqual(decoded, message);
}

void roundTripsReady() {
  mcpd3_distributed::ReadyMessage message;
  message.worker_name = "worker-ready";
  const auto decoded = mcpd3_distributed::decodeReady(
      mcpd3_distributed::encodeReady(message));
  require(decoded.worker_name == message.worker_name, "ready mismatch");
}

void roundTripsSolveRoundRequest() {
  mcpd3::PartitionSolveRequest message;
  message.round_id = 123;
  message.partition_id = 9;
  message.scale = 1000;
  message.regularization_strength = 10;
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

  const auto decoded = mcpd3_distributed::decodeSolveRoundRequest(
      mcpd3_distributed::encodeSolveRoundRequest(message));
  require(decoded.round_id == message.round_id, "request round mismatch");
  require(decoded.partition_id == message.partition_id,
          "request partition mismatch");
  require(decoded.scale == message.scale, "request scale mismatch");
  require(decoded.regularization_strength == message.regularization_strength,
          "request regularization mismatch");
  require(decoded.alpha_updates.size() == message.alpha_updates.size(),
          "request alpha count mismatch");
  for (size_t i = 0; i < message.alpha_updates.size(); ++i) {
    requireAlphaUpdateEqual(decoded.alpha_updates[i], message.alpha_updates[i]);
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
  second.alpha_updates.push_back(
      mcpd3::AlphaUpdate{/*constraint_id=*/4,
                          /*alpha=*/5,
                          /*last_alpha=*/-6,
                          /*alpha_momentum=*/-1.5f});

  const std::vector<mcpd3::PartitionSolveRequest> messages{first, second};
  const auto decoded = mcpd3_distributed::decodeSolveRoundBatchRequest(
      mcpd3_distributed::encodeSolveRoundBatchRequest(messages));

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
    require(decoded[i].alpha_updates.size() ==
                messages[i].alpha_updates.size(),
            "batch request alpha count mismatch");
    for (size_t j = 0; j < messages[i].alpha_updates.size(); ++j) {
      requireAlphaUpdateEqual(decoded[i].alpha_updates[j],
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

  const auto decoded = mcpd3_distributed::decodeSolveRoundResult(
      mcpd3_distributed::encodeSolveRoundResult(message));
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
    requireLabelEqual(decoded.constrained_labels[i],
                      message.constrained_labels[i]);
  }

  const auto decoded_default_timing =
      mcpd3_distributed::decodeTimedSolveRoundResult(
          mcpd3_distributed::encodeSolveRoundResult(message));
  require(decoded_default_timing.worker_solve_wall_us == 0,
          "default result timing should be zero");

  const auto decoded_timed = mcpd3_distributed::decodeTimedSolveRoundResult(
      mcpd3_distributed::encodeSolveRoundResultWithTiming(
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

  const std::vector<mcpd3::PartitionSolveResult> messages{first, second};
  const auto decoded = mcpd3_distributed::decodeSolveRoundBatchResult(
      mcpd3_distributed::encodeSolveRoundBatchResult(messages));
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
      requireLabelEqual(decoded[i].constrained_labels[j],
                        messages[i].constrained_labels[j]);
    }
  }

  const auto decoded_default_timing =
      mcpd3_distributed::decodeTimedSolveRoundBatchResult(
          mcpd3_distributed::encodeSolveRoundBatchResult(messages));
  require(decoded_default_timing.worker_solve_wall_us == 0,
          "default batch timing should be zero");

  const auto decoded_timed =
      mcpd3_distributed::decodeTimedSolveRoundBatchResult(
          mcpd3_distributed::encodeSolveRoundBatchResultWithTiming(
              messages, /*worker_solve_wall_us=*/56789));
  require(decoded_timed.results.size() == messages.size(),
          "timed batch result count mismatch");
  require(decoded_timed.results[1].partition_id == second.partition_id,
          "timed batch result payload mismatch");
  require(decoded_timed.worker_solve_wall_us == 56789,
          "timed batch worker solve timing mismatch");
}

void roundTripsScaleObjective() {
  mcpd3_distributed::ScaleObjectiveMessage message;
  message.factor = 10;
  const auto decoded = mcpd3_distributed::decodeScaleObjective(
      mcpd3_distributed::encodeScaleObjective(message));
  require(decoded.factor == message.factor, "scale objective factor mismatch");
}

void roundTripsAlphaUpdate() {
  mcpd3_distributed::AlphaUpdateMessage message;
  message.partition_id = 5;
  message.alpha_updates.push_back(
      mcpd3::AlphaUpdate{/*constraint_id=*/6,
                          /*alpha=*/7,
                          /*last_alpha=*/8,
                          /*alpha_momentum=*/2.25f});
  const auto decoded = mcpd3_distributed::decodeAlphaUpdate(
      mcpd3_distributed::encodeAlphaUpdate(message));
  require(decoded.partition_id == message.partition_id,
          "alpha message partition mismatch");
  require(decoded.alpha_updates.size() == 1, "alpha message count mismatch");
  requireAlphaUpdateEqual(decoded.alpha_updates[0], message.alpha_updates[0]);
}

void roundTripsStop() {
  mcpd3_distributed::StopMessage message;
  message.reason = 12;
  message.message = "done";
  const auto decoded = mcpd3_distributed::decodeStop(
      mcpd3_distributed::encodeStop(message));
  require(decoded.reason == message.reason, "stop reason mismatch");
  require(decoded.message == message.message, "stop message mismatch");
}

void roundTripsError() {
  mcpd3_distributed::ErrorMessage message;
  message.code = 99;
  message.message = "bad frame";
  const auto decoded = mcpd3_distributed::decodeError(
      mcpd3_distributed::encodeError(message));
  require(decoded.code == message.code, "error code mismatch");
  require(decoded.message == message.message, "error message mismatch");
}

void rejectsMalformedFrames() {
  requireThrows([] { mcpd3_distributed::decodeFrame({1, 0, 0}); },
                "truncated frame header should fail");

  auto unknown = mcpd3_distributed::encodeFrame(
      mcpd3_distributed::MessageType::HELLO, {});
  unknown[0] = 250;
  requireThrows([&] { mcpd3_distributed::decodeFrame(unknown); },
                "unknown message type should fail");

  auto size_mismatch = mcpd3_distributed::encodeFrame(
      mcpd3_distributed::MessageType::READY, {1, 2, 3});
  size_mismatch.pop_back();
  requireThrows([&] { mcpd3_distributed::decodeFrame(size_mismatch); },
                "payload size mismatch should fail");

  requireThrows(
      [] {
        mcpd3_distributed::decodeReady(
            mcpd3_distributed::encodeFrame(
                mcpd3_distributed::MessageType::ERROR, {}));
      },
      "wrong message type should fail");

  auto truncated_payload = mcpd3_distributed::encodeReady(
      mcpd3_distributed::ReadyMessage{"worker"});
  truncated_payload[12] = 200;
  requireThrows([&] { mcpd3_distributed::decodeReady(truncated_payload); },
                "truncated payload should fail");

  auto trailing_payload = mcpd3_distributed::encodeFrame(
      mcpd3_distributed::MessageType::SCALE_OBJECTIVE,
      std::vector<std::uint8_t>{1, 0, 0, 0, 0, 0, 0, 0, 99});
  requireThrows([&] {
    mcpd3_distributed::decodeScaleObjective(trailing_payload);
  }, "trailing payload bytes should fail");
}

} // namespace

int main() {
  try {
    frameHeaderIsLittleEndian();
    roundTripsHello();
    roundTripsPartitionPackage();
    roundTripsReady();
    roundTripsSolveRoundRequest();
    roundTripsSolveRoundBatchRequest();
    roundTripsSolveRoundResult();
    roundTripsSolveRoundBatchResult();
    roundTripsScaleObjective();
    roundTripsAlphaUpdate();
    roundTripsStop();
    roundTripsError();
    rejectsMalformedFrames();
  } catch (const std::exception &e) {
    std::cerr << "protocol_serialization_test failed: " << e.what() << "\n";
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
