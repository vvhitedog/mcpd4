#include <mcpd4/protocol.h>

#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <type_traits>

namespace mcpd4 {
namespace {

constexpr std::size_t kFrameHeaderBytes = 12;

void require(bool condition, const std::string &message) {
  if (!condition) {
    throw std::runtime_error(message);
  }
}

bool isKnownMessageType(std::uint32_t value) {
  switch (static_cast<MessageType>(value)) {
  case MessageType::HELLO:
  case MessageType::PARTITION_PACKAGE:
  case MessageType::READY:
  case MessageType::SOLVE_ROUND_REQUEST:
  case MessageType::SOLVE_ROUND_RESULT:
  case MessageType::SCALE_OBJECTIVE:
  case MessageType::ALPHA_UPDATE:
  case MessageType::STOP:
  case MessageType::ERROR:
  case MessageType::SOLVE_ROUND_BATCH_REQUEST:
  case MessageType::SOLVE_ROUND_BATCH_RESULT:
    return true;
  }
  return false;
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

class Writer {
public:
  const std::vector<std::uint8_t> &bytes() const { return bytes_; }

  void reserve(std::size_t size) { bytes_.reserve(size); }

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

  void writeFloat(float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t),
                  "float serialization expects 32-bit float");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(bits);
  }

  void writeString(const std::string &value) {
    writeU32(checkedSize(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  template <typename T, typename Fn>
  void writeVector(const std::vector<T> &values, Fn write_one) {
    writeU32(checkedSize(values.size()));
    for (const auto &value : values) {
      write_one(value);
    }
  }

  void writeI32Vector(const std::vector<int> &values) {
    writeU32(checkedSize(values.size()));
    if (values.empty()) {
      return;
    }
    if (hostIsLittleEndian() && sizeof(int) == sizeof(std::int32_t)) {
      const auto *begin =
          reinterpret_cast<const std::uint8_t *>(values.data());
      bytes_.insert(bytes_.end(), begin,
                    begin + values.size() * sizeof(std::int32_t));
      return;
    }
    for (const auto value : values) {
      writeI32(value);
    }
  }

private:
  static bool hostIsLittleEndian() {
    const std::uint32_t value = 1;
    return *reinterpret_cast<const std::uint8_t *>(&value) == 1;
  }

  std::vector<std::uint8_t> bytes_;
};

class Reader {
public:
  explicit Reader(const std::vector<std::uint8_t> &bytes)
      : bytes_(bytes), end_(bytes.size()) {}

  Reader(const std::vector<std::uint8_t> &bytes, std::size_t offset,
         std::size_t size)
      : bytes_(bytes), offset_(offset), end_(offset + size) {
    require(offset <= bytes_.size() && size <= bytes_.size() - offset,
            "payload view is outside frame bounds");
  }

  bool empty() const { return offset_ == end_; }

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

  float readFloat() {
    static_assert(sizeof(float) == sizeof(std::uint32_t),
                  "float serialization expects 32-bit float");
    const auto bits = readU32();
    float value = 0;
    std::memcpy(&value, &bits, sizeof(value));
    return value;
  }

  std::string readString() {
    const auto size = readU32();
    requireRemaining(size);
    std::string value(bytes_.begin() + static_cast<std::ptrdiff_t>(offset_),
                      bytes_.begin() +
                          static_cast<std::ptrdiff_t>(offset_ + size));
    offset_ += size;
    return value;
  }

  template <typename T, typename Fn> std::vector<T> readVector(Fn read_one) {
    const auto size = readU32();
    std::vector<T> values;
    for (std::uint32_t i = 0; i < size; ++i) {
      values.push_back(read_one());
    }
    return values;
  }

  std::vector<int> readI32Vector() {
    const auto size = readU32();
    std::vector<int> values(static_cast<std::size_t>(size));
    if (values.empty()) {
      return values;
    }
    if (hostIsLittleEndian() && sizeof(int) == sizeof(std::int32_t)) {
      const auto byte_count =
          static_cast<std::size_t>(size) * sizeof(std::int32_t);
      requireRemaining(byte_count);
      std::memcpy(values.data(), bytes_.data() + offset_, byte_count);
      offset_ += byte_count;
      return values;
    }
    for (std::uint32_t i = 0; i < size; ++i) {
      values[static_cast<std::size_t>(i)] = readI32();
    }
    return values;
  }

private:
  void requireRemaining(std::size_t count) const {
    if (count > end_ - offset_) {
      throw std::runtime_error("truncated payload");
    }
  }

  static bool hostIsLittleEndian() {
    const std::uint32_t value = 1;
    return *reinterpret_cast<const std::uint8_t *>(&value) == 1;
  }

  const std::vector<std::uint8_t> &bytes_;
  std::size_t offset_ = 0;
  std::size_t end_ = 0;
};

void requireDone(const Reader &reader) {
  require(reader.empty(), "payload has trailing bytes");
}

struct DecodedFrameView {
  MessageType type = MessageType::ERROR;
  std::size_t payload_offset = kFrameHeaderBytes;
  std::size_t payload_size = 0;
};

DecodedFrameView decodeFrameView(const std::vector<std::uint8_t> &bytes) {
  require(bytes.size() >= kFrameHeaderBytes, "frame header is truncated");
  Reader reader(bytes);
  const auto raw_type = reader.readU32();
  require(isKnownMessageType(raw_type), "unknown message type");
  const auto payload_size = reader.readU64();
  require(payload_size <= std::numeric_limits<std::size_t>::max(),
          "payload size is too large");
  require(bytes.size() - kFrameHeaderBytes ==
              static_cast<std::size_t>(payload_size),
          "frame payload size does not match buffer size");
  DecodedFrameView view;
  view.type = static_cast<MessageType>(raw_type);
  view.payload_size = static_cast<std::size_t>(payload_size);
  return view;
}

Frame decodeExpectedFrame(const std::vector<std::uint8_t> &frame,
                          MessageType expected_type) {
  auto decoded = decodeFrame(frame);
  require(decoded.type == expected_type, "unexpected message type");
  return decoded;
}

DecodedFrameView decodeExpectedFrameView(
    const std::vector<std::uint8_t> &frame, MessageType expected_type) {
  auto decoded = decodeFrameView(frame);
  require(decoded.type == expected_type, "unexpected message type");
  return decoded;
}

void writeAlphaUpdate(Writer *writer, const mcpd3::AlphaUpdate &update) {
  writer->writeI32(update.constraint_id);
  writer->writeI64(update.alpha);
}

mcpd3::AlphaUpdate readAlphaUpdate(Reader *reader) {
  mcpd3::AlphaUpdate update;
  update.constraint_id = reader->readI32();
  update.alpha = checkedIntegerCast<long>(reader->readI64());
  return update;
}

void writeConstraintEndpoint(Writer *writer,
                             const mcpd3::ConstraintEndpointBinding &binding) {
  writer->writeI32(binding.constraint_id);
  writer->writeI32(binding.global_node_id);
  writer->writeI32(binding.local_index);
  writer->writeBool(binding.is_source);
  writer->writeI64(binding.alpha);
  writer->writeI64(binding.last_alpha);
  writer->writeFloat(binding.alpha_momentum);
}

mcpd3::ConstraintEndpointBinding readConstraintEndpoint(Reader *reader) {
  mcpd3::ConstraintEndpointBinding binding;
  binding.constraint_id = reader->readI32();
  binding.global_node_id = reader->readI32();
  binding.local_index = reader->readI32();
  binding.is_source = reader->readBool();
  binding.alpha = checkedIntegerCast<long>(reader->readI64());
  binding.last_alpha = checkedIntegerCast<long>(reader->readI64());
  binding.alpha_momentum = reader->readFloat();
  return binding;
}

void writeConstraintLabel(Writer *writer, const mcpd3::ConstraintLabel &label) {
  writer->writeI32(label.constraint_id);
  writer->writeI32(label.label);
}

mcpd3::ConstraintLabel readConstraintLabel(Reader *reader) {
  mcpd3::ConstraintLabel label;
  label.constraint_id = reader->readI32();
  label.label = reader->readI32();
  return label;
}

void writeSolveRoundRequestPayload(
    Writer *writer, const mcpd3::PartitionSolveRequest &message) {
  writer->writeI64(message.round_id);
  writer->writeI32(message.partition_id);
  writer->writeI64(message.scale);
  writer->writeI32(message.regularization_strength);
  writer->writeVector<mcpd3::AlphaUpdate>(
      message.alpha_updates,
      [&](const auto &update) { writeAlphaUpdate(writer, update); });
}

mcpd3::PartitionSolveRequest readSolveRoundRequestPayload(Reader *reader) {
  mcpd3::PartitionSolveRequest message;
  message.round_id = checkedIntegerCast<long>(reader->readI64());
  message.partition_id = reader->readI32();
  message.scale = checkedIntegerCast<long>(reader->readI64());
  message.regularization_strength = reader->readI32();
  message.alpha_updates = reader->readVector<mcpd3::AlphaUpdate>(
      [&] { return readAlphaUpdate(reader); });
  return message;
}

std::size_t partitionPackageFrameSize(
    const mcpd3::PartitionPackage &message) {
  constexpr std::size_t kIntVectorHeaderBytes = 4;
  constexpr std::size_t kIntBytes = 4;
  constexpr std::size_t kConstraintEndpointBytes =
      4 + 4 + 4 + 1 + 8 + 8 + 4;
  return kFrameHeaderBytes +
         /*partition_id + local_node_count=*/8 +
         kIntVectorHeaderBytes + message.arcs.size() * kIntBytes +
         kIntVectorHeaderBytes + message.arc_capacities.size() * kIntBytes +
         kIntVectorHeaderBytes +
         message.terminal_capacities.size() * kIntBytes +
         kIntVectorHeaderBytes + message.local_to_global.size() * kIntBytes +
         /*constraint endpoint vector header=*/4 +
         message.constraint_endpoints.size() * kConstraintEndpointBytes;
}

void writeSolveRoundResultPayload(
    Writer *writer, const mcpd3::PartitionSolveResult &message) {
  writer->writeI64(message.round_id);
  writer->writeI32(message.partition_id);
  writer->writeI64(message.lower_bound);
  writer->writeI64(message.regularization_budget);
  writer->writeI64(message.regularization_contribution);
  writer->writeI64(message.regularization_anchor_sink_count);
  writer->writeI64(message.regularization_active_sink_count);
  writer->writeVector<mcpd3::ConstraintLabel>(
      message.constrained_labels,
      [&](const auto &label) { writeConstraintLabel(writer, label); });
}

mcpd3::PartitionSolveResult readSolveRoundResultPayload(Reader *reader) {
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
  message.constrained_labels = reader->readVector<mcpd3::ConstraintLabel>(
      [&] { return readConstraintLabel(reader); });
  return message;
}

} // namespace

std::vector<std::uint8_t> encodeFrame(
    MessageType type, const std::vector<std::uint8_t> &payload) {
  Writer writer;
  writer.writeU32(static_cast<std::uint32_t>(type));
  writer.writeU64(payload.size());
  auto bytes = writer.bytes();
  bytes.insert(bytes.end(), payload.begin(), payload.end());
  return bytes;
}

Frame decodeFrame(const std::vector<std::uint8_t> &bytes) {
  const auto view = decodeFrameView(bytes);
  Frame frame;
  frame.type = view.type;
  frame.payload.assign(
      bytes.begin() + static_cast<std::ptrdiff_t>(view.payload_offset),
      bytes.begin() + static_cast<std::ptrdiff_t>(view.payload_offset +
                                                  view.payload_size));
  return frame;
}

MessageType decodeFrameType(const std::vector<std::uint8_t> &bytes) {
  return decodeFrameView(bytes).type;
}

std::vector<std::uint8_t> encodeHello(const HelloMessage &message) {
  Writer writer;
  writer.writeU32(message.protocol_version);
  writer.writeString(message.worker_name);
  writer.writeU32(message.cpu_count);
  writer.writeU64(message.ram_gb);
  writer.writeU64(message.feature_bits);
  writer.writeString(message.temp_path);
  writer.writeBool(message.debug_build);
  writer.writeBool(message.little_endian);
  return encodeFrame(MessageType::HELLO, writer.bytes());
}

HelloMessage decodeHello(const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::HELLO);
  Reader reader(decoded.payload);
  HelloMessage message;
  message.protocol_version = reader.readU32();
  message.worker_name = reader.readString();
  message.cpu_count = reader.readU32();
  message.ram_gb = reader.readU64();
  message.feature_bits = reader.readU64();
  message.temp_path = reader.readString();
  message.debug_build = reader.readBool();
  message.little_endian = reader.readBool();
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodePartitionPackage(
    const mcpd3::PartitionPackage &message) {
  const auto frame_size = partitionPackageFrameSize(message);
  Writer writer;
  writer.reserve(frame_size);
  writer.writeU32(static_cast<std::uint32_t>(MessageType::PARTITION_PACKAGE));
  writer.writeU64(frame_size - kFrameHeaderBytes);
  writer.writeI32(message.partition_id);
  writer.writeI32(message.local_node_count);
  writer.writeI32Vector(message.arcs);
  writer.writeI32Vector(message.arc_capacities);
  writer.writeI32Vector(message.terminal_capacities);
  writer.writeI32Vector(message.local_to_global);
  writer.writeVector<mcpd3::ConstraintEndpointBinding>(
      message.constraint_endpoints,
      [&](const auto &binding) { writeConstraintEndpoint(&writer, binding); });
  return writer.bytes();
}

mcpd3::PartitionPackage decodePartitionPackage(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrameView(frame, MessageType::PARTITION_PACKAGE);
  Reader reader(frame, decoded.payload_offset, decoded.payload_size);
  mcpd3::PartitionPackage message;
  message.partition_id = reader.readI32();
  message.local_node_count = reader.readI32();
  message.arcs = reader.readI32Vector();
  message.arc_capacities = reader.readI32Vector();
  message.terminal_capacities = reader.readI32Vector();
  message.local_to_global = reader.readI32Vector();
  message.constraint_endpoints =
      reader.readVector<mcpd3::ConstraintEndpointBinding>(
          [&] { return readConstraintEndpoint(&reader); });
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeReady(const ReadyMessage &message) {
  Writer writer;
  writer.writeString(message.worker_name);
  return encodeFrame(MessageType::READY, writer.bytes());
}

ReadyMessage decodeReady(const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::READY);
  Reader reader(decoded.payload);
  ReadyMessage message;
  message.worker_name = reader.readString();
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeSolveRoundRequest(
    const mcpd3::PartitionSolveRequest &message) {
  Writer writer;
  writeSolveRoundRequestPayload(&writer, message);
  return encodeFrame(MessageType::SOLVE_ROUND_REQUEST, writer.bytes());
}

mcpd3::PartitionSolveRequest decodeSolveRoundRequest(
    const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::SOLVE_ROUND_REQUEST);
  Reader reader(decoded.payload);
  auto message = readSolveRoundRequestPayload(&reader);
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeSolveRoundBatchRequest(
    const std::vector<mcpd3::PartitionSolveRequest> &messages) {
  Writer writer;
  writer.writeVector<mcpd3::PartitionSolveRequest>(
      messages, [&](const auto &message) {
        writeSolveRoundRequestPayload(&writer, message);
      });
  return encodeFrame(MessageType::SOLVE_ROUND_BATCH_REQUEST, writer.bytes());
}

std::vector<mcpd3::PartitionSolveRequest> decodeSolveRoundBatchRequest(
    const std::vector<std::uint8_t> &frame) {
  auto decoded =
      decodeExpectedFrame(frame, MessageType::SOLVE_ROUND_BATCH_REQUEST);
  Reader reader(decoded.payload);
  auto messages = reader.readVector<mcpd3::PartitionSolveRequest>(
      [&] { return readSolveRoundRequestPayload(&reader); });
  requireDone(reader);
  return messages;
}

std::vector<std::uint8_t> encodeSolveRoundResultWithTiming(
    const mcpd3::PartitionSolveResult &message,
    std::uint64_t worker_solve_wall_us) {
  Writer writer;
  writeSolveRoundResultPayload(&writer, message);
  writer.writeU64(worker_solve_wall_us);
  return encodeFrame(MessageType::SOLVE_ROUND_RESULT, writer.bytes());
}

std::vector<std::uint8_t> encodeSolveRoundResult(
    const mcpd3::PartitionSolveResult &message) {
  return encodeSolveRoundResultWithTiming(message,
                                          /*worker_solve_wall_us=*/0);
}

TimedSolveRoundResult decodeTimedSolveRoundResult(
    const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::SOLVE_ROUND_RESULT);
  Reader reader(decoded.payload);
  TimedSolveRoundResult timed;
  timed.result = readSolveRoundResultPayload(&reader);
  if (!reader.empty()) {
    timed.worker_solve_wall_us = reader.readU64();
  }
  requireDone(reader);
  return timed;
}

mcpd3::PartitionSolveResult decodeSolveRoundResult(
    const std::vector<std::uint8_t> &frame) {
  return decodeTimedSolveRoundResult(frame).result;
}

std::vector<std::uint8_t> encodeSolveRoundBatchResultWithTiming(
    const std::vector<mcpd3::PartitionSolveResult> &messages,
    std::uint64_t worker_solve_wall_us) {
  Writer writer;
  writer.writeVector<mcpd3::PartitionSolveResult>(
      messages, [&](const auto &message) {
        writeSolveRoundResultPayload(&writer, message);
      });
  writer.writeU64(worker_solve_wall_us);
  return encodeFrame(MessageType::SOLVE_ROUND_BATCH_RESULT, writer.bytes());
}

std::vector<std::uint8_t> encodeSolveRoundBatchResult(
    const std::vector<mcpd3::PartitionSolveResult> &messages) {
  return encodeSolveRoundBatchResultWithTiming(messages,
                                               /*worker_solve_wall_us=*/0);
}

TimedSolveRoundBatchResult decodeTimedSolveRoundBatchResult(
    const std::vector<std::uint8_t> &frame) {
  auto decoded =
      decodeExpectedFrame(frame, MessageType::SOLVE_ROUND_BATCH_RESULT);
  Reader reader(decoded.payload);
  TimedSolveRoundBatchResult timed;
  timed.results = reader.readVector<mcpd3::PartitionSolveResult>(
      [&] { return readSolveRoundResultPayload(&reader); });
  if (!reader.empty()) {
    timed.worker_solve_wall_us = reader.readU64();
  }
  requireDone(reader);
  return timed;
}

std::vector<mcpd3::PartitionSolveResult> decodeSolveRoundBatchResult(
    const std::vector<std::uint8_t> &frame) {
  return decodeTimedSolveRoundBatchResult(frame).results;
}

std::vector<std::uint8_t> encodeScaleObjective(
    const ScaleObjectiveMessage &message) {
  Writer writer;
  writer.writeI64(message.factor);
  writer.writeBool(message.saturate_capacity_overflow);
  return encodeFrame(MessageType::SCALE_OBJECTIVE, writer.bytes());
}

ScaleObjectiveMessage decodeScaleObjective(
    const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::SCALE_OBJECTIVE);
  Reader reader(decoded.payload);
  ScaleObjectiveMessage message;
  message.factor = reader.readI64();
  message.saturate_capacity_overflow = reader.readBool();
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeAlphaUpdate(
    const AlphaUpdateMessage &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeVector<mcpd3::AlphaUpdate>(
      message.alpha_updates,
      [&](const auto &update) { writeAlphaUpdate(&writer, update); });
  return encodeFrame(MessageType::ALPHA_UPDATE, writer.bytes());
}

AlphaUpdateMessage decodeAlphaUpdate(const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::ALPHA_UPDATE);
  Reader reader(decoded.payload);
  AlphaUpdateMessage message;
  message.partition_id = reader.readI32();
  message.alpha_updates = reader.readVector<mcpd3::AlphaUpdate>(
      [&] { return readAlphaUpdate(&reader); });
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeStop(const StopMessage &message) {
  Writer writer;
  writer.writeU32(message.reason);
  writer.writeString(message.message);
  return encodeFrame(MessageType::STOP, writer.bytes());
}

StopMessage decodeStop(const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::STOP);
  Reader reader(decoded.payload);
  StopMessage message;
  message.reason = reader.readU32();
  message.message = reader.readString();
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeError(const ErrorMessage &message) {
  Writer writer;
  writer.writeU32(message.code);
  writer.writeString(message.message);
  return encodeFrame(MessageType::ERROR, writer.bytes());
}

ErrorMessage decodeError(const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::ERROR);
  Reader reader(decoded.payload);
  ErrorMessage message;
  message.code = reader.readU32();
  message.message = reader.readString();
  requireDone(reader);
  return message;
}

} // namespace mcpd4
