#include <mcpd4/delta_codec.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include <type_traits>

namespace mcpd4 {
namespace {

struct EncodedAlphaUpdate {
  int constraint_id = -1;
  std::int64_t value = 0;
};

struct EncodedConstraintLabel {
  int constraint_id = -1;
  int label = 0;
};

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

template <typename T> T checkedIntegerCast(std::int64_t value) {
  static_assert(std::is_integral<T>::value, "T must be integral");
  if (value < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
      value > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
    throw std::runtime_error("integer value is outside target range");
  }
  return static_cast<T>(value);
}

template <typename T> std::uint32_t checkedSize(T size) {
  if (size > static_cast<T>(std::numeric_limits<std::uint32_t>::max())) {
    throw std::runtime_error("container is too large to serialize");
  }
  return static_cast<std::uint32_t>(size);
}

std::int64_t checkedLongDifference(long lhs, long rhs) {
  const auto diff = static_cast<__int128>(lhs) - static_cast<__int128>(rhs);
  if (diff < static_cast<__int128>(std::numeric_limits<std::int64_t>::min()) ||
      diff > static_cast<__int128>(std::numeric_limits<std::int64_t>::max())) {
    throw std::overflow_error("temporal alpha delta overflow");
  }
  return static_cast<std::int64_t>(diff);
}

long checkedLongSum(long lhs, std::int64_t rhs) {
  const auto sum = static_cast<__int128>(lhs) + static_cast<__int128>(rhs);
  if (sum < static_cast<__int128>(std::numeric_limits<long>::min()) ||
      sum > static_cast<__int128>(std::numeric_limits<long>::max())) {
    throw std::overflow_error("temporal alpha reconstruction overflow");
  }
  return static_cast<long>(sum);
}

std::uint64_t zigZagEncode(std::int64_t value) {
  return (static_cast<std::uint64_t>(value) << 1) ^
         static_cast<std::uint64_t>(-(value < 0));
}

std::int64_t zigZagDecode(std::uint64_t value) {
  const auto decoded =
      (value >> 1) ^ static_cast<std::uint64_t>(-(value & 1));
  std::int64_t result = 0;
  std::memcpy(&result, &decoded, sizeof(result));
  return result;
}

class Writer {
public:
  const std::vector<std::uint8_t> &bytes() const { return bytes_; }

  void writeU8(std::uint8_t value) { bytes_.push_back(value); }

  void writeU32(std::uint32_t value) {
    for (int shift = 0; shift < 32; shift += 8) {
      bytes_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
  }

  void writeU64(std::uint64_t value) {
    for (int shift = 0; shift < 64; shift += 8) {
      bytes_.push_back(static_cast<std::uint8_t>((value >> shift) & 0xff));
    }
  }

  void writeI32(int value) {
    const auto signed_value = static_cast<std::int32_t>(value);
    std::uint32_t bits = 0;
    std::memcpy(&bits, &signed_value, sizeof(bits));
    writeU32(bits);
  }

  void writeI64(std::int64_t value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU64(bits);
  }

  void writeBool(bool value) { writeU8(value ? 1 : 0); }

  void writeVarU64(std::uint64_t value) {
    while (value >= 0x80) {
      writeU8(static_cast<std::uint8_t>((value & 0x7f) | 0x80));
      value >>= 7;
    }
    writeU8(static_cast<std::uint8_t>(value));
  }

  void writeVarI64(std::int64_t value) { writeVarU64(zigZagEncode(value)); }

  template <typename T, typename Fn>
  void writeVector(const std::vector<T> &values, Fn write_one) {
    writeU32(checkedSize(values.size()));
    for (const auto &value : values) {
      write_one(value);
    }
  }

private:
  std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
  explicit Reader(const std::vector<std::uint8_t> &bytes) : bytes_(bytes) {}

  bool empty() const { return offset_ == bytes_.size(); }

  std::uint8_t readU8() {
    requireRemaining(1);
    return bytes_[offset_++];
  }

  std::uint32_t readU32() {
    requireRemaining(4);
    std::uint32_t value = 0;
    for (int shift = 0; shift < 32; shift += 8) {
      value |= static_cast<std::uint32_t>(bytes_[offset_++]) << shift;
    }
    return value;
  }

  std::uint64_t readU64() {
    requireRemaining(8);
    std::uint64_t value = 0;
    for (int shift = 0; shift < 64; shift += 8) {
      value |= static_cast<std::uint64_t>(bytes_[offset_++]) << shift;
    }
    return value;
  }

  int readI32() {
    const auto bits = readU32();
    std::int32_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  std::int64_t readI64() {
    const auto bits = readU64();
    std::int64_t value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  bool readBool() {
    const auto value = readU8();
    require(value == 0 || value == 1, "invalid boolean value");
    return value != 0;
  }

  std::uint64_t readVarU64() {
    std::uint64_t value = 0;
    for (int i = 0; i < 10; ++i) {
      const auto byte = readU8();
      if (i == 9 && (byte & 0x7e) != 0) {
        throw std::runtime_error("varint value is too large");
      }
      value |= static_cast<std::uint64_t>(byte & 0x7f) << (7 * i);
      if ((byte & 0x80) == 0) {
        return value;
      }
    }
    throw std::runtime_error("varint value is too long");
  }

  std::int64_t readVarI64() { return zigZagDecode(readVarU64()); }

  template <typename T, typename Fn> std::vector<T> readVector(Fn read_one) {
    const auto size = readU32();
    std::vector<T> values;
    values.reserve(size);
    for (std::uint32_t i = 0; i < size; ++i) {
      values.push_back(read_one());
    }
    return values;
  }

private:
  void requireRemaining(std::size_t count) const {
    if (count > bytes_.size() - offset_) {
      throw std::runtime_error("truncated payload");
    }
  }

  const std::vector<std::uint8_t> &bytes_;
  std::size_t offset_ = 0;
};

void requireDone(const Reader &reader) {
  require(reader.empty(), "payload has trailing bytes");
}

Frame decodeExpectedFrame(const std::vector<std::uint8_t> &frame,
                          MessageType expected_type) {
  auto decoded = decodeFrame(frame);
  require(decoded.type == expected_type, "unexpected message type");
  return decoded;
}

TemporalSolveCodecState *requireState(TemporalSolveCodecState *state) {
  require(state != nullptr, "temporal codec state is required");
  return state;
}

void writeEncodedAlpha(Writer *writer, const EncodedAlphaUpdate &update) {
  writer->writeI32(update.constraint_id);
  writer->writeVarI64(update.value);
}

EncodedAlphaUpdate readEncodedAlpha(Reader *reader) {
  EncodedAlphaUpdate update;
  update.constraint_id = reader->readI32();
  update.value = reader->readVarI64();
  return update;
}

std::vector<EncodedAlphaUpdate> encodeAlphaUpdatesForPartition(
    const mcpd3::PartitionSolveRequest &message,
    TemporalSolveCodecState *state) {
  auto &partition_state = requireState(state)->partitions[message.partition_id];
  std::vector<EncodedAlphaUpdate> encoded;
  encoded.reserve(message.alpha_updates.size());
  for (const auto &update : message.alpha_updates) {
    auto find_iter =
        partition_state.alpha_by_constraint.find(update.constraint_id);
    if (find_iter != partition_state.alpha_by_constraint.end() &&
        find_iter->second == update.alpha) {
      continue;
    }
    EncodedAlphaUpdate wire_update;
    wire_update.constraint_id = update.constraint_id;
    wire_update.value =
        find_iter == partition_state.alpha_by_constraint.end()
            ? checkedIntegerCast<std::int64_t>(update.alpha)
            : checkedLongDifference(update.alpha, find_iter->second);
    partition_state.alpha_by_constraint[update.constraint_id] = update.alpha;
    encoded.push_back(wire_update);
  }
  return encoded;
}

std::vector<mcpd3::AlphaUpdate> decodeAlphaUpdatesForPartition(
    int partition_id, const std::vector<EncodedAlphaUpdate> &encoded,
    TemporalSolveCodecState *state) {
  auto &partition_state = requireState(state)->partitions[partition_id];
  std::vector<mcpd3::AlphaUpdate> updates;
  updates.reserve(encoded.size());
  for (const auto &wire_update : encoded) {
    auto find_iter =
        partition_state.alpha_by_constraint.find(wire_update.constraint_id);
    const long alpha =
        find_iter == partition_state.alpha_by_constraint.end()
            ? checkedIntegerCast<long>(wire_update.value)
            : checkedLongSum(find_iter->second, wire_update.value);
    partition_state.alpha_by_constraint[wire_update.constraint_id] = alpha;
    updates.push_back(mcpd3::AlphaUpdate{
        /*constraint_id=*/wire_update.constraint_id,
        /*alpha=*/alpha,
        /*last_alpha=*/0,
        /*alpha_momentum=*/0});
  }
  return updates;
}

void writeSolveRoundRequestPayload(
    Writer *writer, const mcpd3::PartitionSolveRequest &message,
    TemporalSolveCodecState *state) {
  writer->writeI64(message.round_id);
  writer->writeI32(message.partition_id);
  writer->writeI64(message.scale);
  writer->writeI32(message.regularization_strength);
  const auto encoded = encodeAlphaUpdatesForPartition(message, state);
  writer->writeVector<EncodedAlphaUpdate>(
      encoded, [&](const auto &update) { writeEncodedAlpha(writer, update); });
}

mcpd3::PartitionSolveRequest readSolveRoundRequestPayload(
    Reader *reader, TemporalSolveCodecState *state) {
  mcpd3::PartitionSolveRequest message;
  message.round_id = checkedIntegerCast<long>(reader->readI64());
  message.partition_id = reader->readI32();
  message.scale = checkedIntegerCast<long>(reader->readI64());
  message.regularization_strength = reader->readI32();
  const auto encoded = reader->readVector<EncodedAlphaUpdate>(
      [&] { return readEncodedAlpha(reader); });
  message.alpha_updates =
      decodeAlphaUpdatesForPartition(message.partition_id, encoded, state);
  return message;
}

void writeEncodedLabel(Writer *writer, const EncodedConstraintLabel &label) {
  require(label.label == 0 || label.label == 1,
          "constraint label must be binary for temporal encoding");
  writer->writeI32(label.constraint_id);
  writer->writeBool(label.label != 0);
}

EncodedConstraintLabel readEncodedLabel(Reader *reader) {
  EncodedConstraintLabel label;
  label.constraint_id = reader->readI32();
  label.label = reader->readBool() ? 1 : 0;
  return label;
}

std::vector<EncodedConstraintLabel> encodeLabelsForPartition(
    const mcpd3::PartitionSolveResult &message, bool *full_sync,
    TemporalSolveCodecState *state) {
  auto &partition_state = requireState(state)->partitions[message.partition_id];
  *full_sync =
      !partition_state.labels_initialized ||
      partition_state.label_order.size() != message.constrained_labels.size();
  if (!*full_sync) {
    for (const auto &label : message.constrained_labels) {
      if (partition_state.label_by_constraint.find(label.constraint_id) ==
          partition_state.label_by_constraint.end()) {
        *full_sync = true;
        break;
      }
    }
  }
  std::vector<EncodedConstraintLabel> encoded;
  encoded.reserve(message.constrained_labels.size());

  if (*full_sync) {
    partition_state.label_by_constraint.clear();
    partition_state.label_order.clear();
    partition_state.labels_initialized = true;
    for (const auto &label : message.constrained_labels) {
      require(label.label == 0 || label.label == 1,
              "constraint label must be binary for temporal encoding");
      if (partition_state.label_by_constraint
              .emplace(label.constraint_id, label.label)
              .second) {
        partition_state.label_order.push_back(label.constraint_id);
      } else {
        throw std::runtime_error("duplicate constraint label in result");
      }
      encoded.push_back(
          EncodedConstraintLabel{/*constraint_id=*/label.constraint_id,
                                 /*label=*/label.label});
    }
    return encoded;
  }

  for (const auto &label : message.constrained_labels) {
    require(label.label == 0 || label.label == 1,
            "constraint label must be binary for temporal encoding");
    auto find_iter =
        partition_state.label_by_constraint.find(label.constraint_id);
    if (find_iter == partition_state.label_by_constraint.end()) {
      partition_state.label_by_constraint[label.constraint_id] = label.label;
      partition_state.label_order.push_back(label.constraint_id);
      encoded.push_back(
          EncodedConstraintLabel{/*constraint_id=*/label.constraint_id,
                                 /*label=*/label.label});
      continue;
    }
    if (find_iter->second == label.label) {
      continue;
    }
    find_iter->second = label.label;
    encoded.push_back(
        EncodedConstraintLabel{/*constraint_id=*/label.constraint_id,
                               /*label=*/label.label});
  }
  return encoded;
}

std::vector<mcpd3::ConstraintLabel> decodeLabelsForPartition(
    int partition_id, bool full_sync,
    const std::vector<EncodedConstraintLabel> &encoded,
    TemporalSolveCodecState *state) {
  auto &partition_state = requireState(state)->partitions[partition_id];
  if (full_sync) {
    partition_state.label_by_constraint.clear();
    partition_state.label_order.clear();
    partition_state.labels_initialized = true;
  } else if (!partition_state.labels_initialized) {
    throw std::runtime_error("label delta received before full sync");
  }

  for (const auto &wire_label : encoded) {
    auto find_iter =
        partition_state.label_by_constraint.find(wire_label.constraint_id);
    if (find_iter == partition_state.label_by_constraint.end()) {
      partition_state.label_by_constraint.emplace(wire_label.constraint_id,
                                                  wire_label.label);
      partition_state.label_order.push_back(wire_label.constraint_id);
    } else {
      find_iter->second = wire_label.label;
    }
  }

  std::vector<mcpd3::ConstraintLabel> labels;
  labels.reserve(partition_state.label_order.size());
  for (const int constraint_id : partition_state.label_order) {
    const auto find_iter =
        partition_state.label_by_constraint.find(constraint_id);
    if (find_iter == partition_state.label_by_constraint.end()) {
      throw std::runtime_error("label order references missing label state");
    }
    labels.push_back(mcpd3::ConstraintLabel{/*constraint_id=*/constraint_id,
                                            /*global_node_id=*/-1,
                                            /*local_index=*/-1,
                                            /*label=*/find_iter->second});
  }
  return labels;
}

void writeSolveRoundResultPayload(
    Writer *writer, const mcpd3::PartitionSolveResult &message,
    TemporalSolveCodecState *state) {
  writer->writeI64(message.round_id);
  writer->writeI32(message.partition_id);
  writer->writeI64(message.lower_bound);
  writer->writeI64(message.regularization_budget);
  writer->writeI64(message.regularization_contribution);
  writer->writeI64(message.regularization_anchor_sink_count);
  writer->writeI64(message.regularization_active_sink_count);
  bool full_sync = false;
  const auto labels = encodeLabelsForPartition(message, &full_sync, state);
  writer->writeBool(full_sync);
  writer->writeVector<EncodedConstraintLabel>(
      labels, [&](const auto &label) { writeEncodedLabel(writer, label); });
}

mcpd3::PartitionSolveResult readSolveRoundResultPayload(
    Reader *reader, TemporalSolveCodecState *state) {
  mcpd3::PartitionSolveResult message;
  message.round_id = checkedIntegerCast<long>(reader->readI64());
  message.partition_id = reader->readI32();
  message.lower_bound = checkedIntegerCast<long>(reader->readI64());
  message.regularization_budget = checkedIntegerCast<long>(reader->readI64());
  message.regularization_contribution =
      checkedIntegerCast<long>(reader->readI64());
  message.regularization_anchor_sink_count =
      checkedIntegerCast<long>(reader->readI64());
  message.regularization_active_sink_count =
      checkedIntegerCast<long>(reader->readI64());
  const bool full_sync = reader->readBool();
  const auto encoded = reader->readVector<EncodedConstraintLabel>(
      [&] { return readEncodedLabel(reader); });
  message.constrained_labels =
      decodeLabelsForPartition(message.partition_id, full_sync, encoded, state);
  return message;
}

} // namespace

void TemporalSolveCodecState::reset() { partitions.clear(); }

void TemporalSolveCodecState::resetPartition(int partition_id) {
  partitions.erase(partition_id);
}

std::vector<std::uint8_t> encodeDeltaSolveRoundRequest(
    const mcpd3::PartitionSolveRequest &message,
    TemporalSolveCodecState *state) {
  Writer writer;
  writeSolveRoundRequestPayload(&writer, message, state);
  return encodeFrame(MessageType::SOLVE_ROUND_REQUEST, writer.bytes());
}

mcpd3::PartitionSolveRequest decodeDeltaSolveRoundRequest(
    const std::vector<std::uint8_t> &frame, TemporalSolveCodecState *state) {
  auto decoded = decodeExpectedFrame(frame, MessageType::SOLVE_ROUND_REQUEST);
  Reader reader(decoded.payload);
  auto message = readSolveRoundRequestPayload(&reader, state);
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeDeltaSolveRoundBatchRequest(
    const std::vector<mcpd3::PartitionSolveRequest> &messages,
    TemporalSolveCodecState *state) {
  Writer writer;
  writer.writeVector<mcpd3::PartitionSolveRequest>(
      messages, [&](const auto &message) {
        writeSolveRoundRequestPayload(&writer, message, state);
      });
  return encodeFrame(MessageType::SOLVE_ROUND_BATCH_REQUEST, writer.bytes());
}

std::vector<mcpd3::PartitionSolveRequest> decodeDeltaSolveRoundBatchRequest(
    const std::vector<std::uint8_t> &frame, TemporalSolveCodecState *state) {
  auto decoded =
      decodeExpectedFrame(frame, MessageType::SOLVE_ROUND_BATCH_REQUEST);
  Reader reader(decoded.payload);
  auto messages = reader.readVector<mcpd3::PartitionSolveRequest>(
      [&] { return readSolveRoundRequestPayload(&reader, state); });
  requireDone(reader);
  return messages;
}

std::vector<std::uint8_t> encodeDeltaSolveRoundResultWithTiming(
    const mcpd3::PartitionSolveResult &message,
    std::uint64_t worker_solve_wall_us, TemporalSolveCodecState *state) {
  Writer writer;
  writeSolveRoundResultPayload(&writer, message, state);
  writer.writeU64(worker_solve_wall_us);
  return encodeFrame(MessageType::SOLVE_ROUND_RESULT, writer.bytes());
}

TimedSolveRoundResult decodeDeltaTimedSolveRoundResult(
    const std::vector<std::uint8_t> &frame, TemporalSolveCodecState *state) {
  auto decoded = decodeExpectedFrame(frame, MessageType::SOLVE_ROUND_RESULT);
  Reader reader(decoded.payload);
  TimedSolveRoundResult timed;
  timed.result = readSolveRoundResultPayload(&reader, state);
  if (!reader.empty()) {
    timed.worker_solve_wall_us = reader.readU64();
  }
  requireDone(reader);
  return timed;
}

std::vector<std::uint8_t> encodeDeltaSolveRoundBatchResultWithTiming(
    const std::vector<mcpd3::PartitionSolveResult> &messages,
    std::uint64_t worker_solve_wall_us, TemporalSolveCodecState *state) {
  Writer writer;
  writer.writeVector<mcpd3::PartitionSolveResult>(
      messages, [&](const auto &message) {
        writeSolveRoundResultPayload(&writer, message, state);
      });
  writer.writeU64(worker_solve_wall_us);
  return encodeFrame(MessageType::SOLVE_ROUND_BATCH_RESULT, writer.bytes());
}

TimedSolveRoundBatchResult decodeDeltaTimedSolveRoundBatchResult(
    const std::vector<std::uint8_t> &frame, TemporalSolveCodecState *state) {
  auto decoded =
      decodeExpectedFrame(frame, MessageType::SOLVE_ROUND_BATCH_RESULT);
  Reader reader(decoded.payload);
  TimedSolveRoundBatchResult timed;
  timed.results = reader.readVector<mcpd3::PartitionSolveResult>(
      [&] { return readSolveRoundResultPayload(&reader, state); });
  if (!reader.empty()) {
    timed.worker_solve_wall_us = reader.readU64();
  }
  requireDone(reader);
  return timed;
}

} // namespace mcpd4
