#include <mcpd4/protocol.h>
#include <mcpd4/integer_codec.h>

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
  case MessageType::LINEAR_STRUCTURE:
  case MessageType::LINEAR_SYSTEM_VALUES:
  case MessageType::LINEAR_INITIALIZE_REQUEST:
  case MessageType::LINEAR_INITIALIZE_RESULT:
  case MessageType::LINEAR_MULTIPLY_REQUEST:
  case MessageType::LINEAR_MULTIPLY_RESULT:
  case MessageType::LINEAR_ALPHA_REQUEST:
  case MessageType::LINEAR_ALPHA_RESULT:
  case MessageType::LINEAR_BETA_REQUEST:
  case MessageType::LINEAR_BETA_RESULT:
  case MessageType::LINEAR_SOLUTION_REQUEST:
  case MessageType::LINEAR_SOLUTION_RESULT:
  case MessageType::REPLACE_PARTITION_CAPACITIES:
  case MessageType::PARTITION_PACKAGE_BEGIN:
  case MessageType::PARTITION_PACKAGE_CHUNK:
  case MessageType::PARTITION_PACKAGE_END:
  case MessageType::FULL_LABELS_REQUEST:
  case MessageType::FULL_LABELS_CHUNK:
  case MessageType::FULL_LABELS_END:
  case MessageType::PARTITION_CAPACITY_UPDATE_BEGIN:
  case MessageType::PARTITION_CAPACITY_UPDATE_CHUNK:
  case MessageType::PARTITION_CAPACITY_UPDATE_END:
    return true;
  }
  return false;
}

bool isKnownCapacityMode(std::uint32_t value) {
  switch (static_cast<CapacityMode>(value)) {
  case CapacityMode::BITS_32:
  case CapacityMode::BITS_64:
  case CapacityMode::BITS_128:
  case CapacityMode::GMP:
    return true;
  }
  return false;
}

bool isKnownCanonicalCutSelection(std::uint32_t value) {
  switch (static_cast<mcpd3::CanonicalCutSelection>(value)) {
  case mcpd3::CanonicalCutSelection::SOLVER_DEFAULT:
  case mcpd3::CanonicalCutSelection::MINIMUM_LABELS:
  case mcpd3::CanonicalCutSelection::MAXIMUM_LABELS:
    return true;
  }
  return false;
}

bool isKnownReferenceCutSelection(std::uint32_t value) {
  switch (static_cast<mcpd3::ReferenceCutSelection>(value)) {
  case mcpd3::ReferenceCutSelection::CLOSEST_EXACT:
  case mcpd3::ReferenceCutSelection::EXACT_REFERENCE_IF_OPTIMAL:
    return true;
  }
  return false;
}

bool isKnownPartitionPackageSection(std::uint32_t value) {
  switch (static_cast<PartitionPackageSection>(value)) {
  case PartitionPackageSection::ARCS:
  case PartitionPackageSection::ARC_CAPACITIES:
  case PartitionPackageSection::TERMINAL_CAPACITIES:
  case PartitionPackageSection::LOCAL_TO_GLOBAL:
  case PartitionPackageSection::CONSTRAINT_ENDPOINTS:
  case PartitionPackageSection::REFERENCE_CUT_LABELS:
    return true;
  }
  return false;
}

bool isKnownPartitionCapacityUpdateSection(std::uint32_t value) {
  switch (static_cast<PartitionCapacityUpdateSection>(value)) {
  case PartitionCapacityUpdateSection::ARC_CAPACITIES:
  case PartitionCapacityUpdateSection::TERMINAL_CAPACITIES:
    return true;
  }
  return false;
}

std::size_t partitionPackageSectionIndex(PartitionPackageSection section) {
  const auto index = static_cast<std::size_t>(section);
  require(index < kPartitionPackageSectionCount,
          "unknown partition package section");
  return index;
}

std::size_t partitionCapacityUpdateSectionIndex(
    PartitionCapacityUpdateSection section) {
  const auto index = static_cast<std::size_t>(section);
  require(index < kPartitionCapacityUpdateSectionCount,
          "unknown partition capacity update section");
  return index;
}

std::size_t checkedContainerSize(std::uint64_t size) {
  require(size <= std::numeric_limits<std::size_t>::max(),
          "partition package section is too large");
  return static_cast<std::size_t>(size);
}

void validatePartitionPackageTransferHeader(
    const PartitionPackageTransferHeader &header) {
  require(header.partition_id >= 0,
          "partition package transfer id must be non-negative");
  require(header.local_node_count >= 0,
          "partition package transfer node count must be non-negative");
  require(header.objective_multiplier > 0,
          "partition package transfer objective multiplier must be positive");
  require(header.reference_cut_check_interval > 0,
          "partition package transfer reference interval must be positive");
  const auto arcs = header.section_counts[static_cast<std::size_t>(
      PartitionPackageSection::ARCS)];
  const auto arc_capacities = header.section_counts[static_cast<std::size_t>(
      PartitionPackageSection::ARC_CAPACITIES)];
  const auto terminal_capacities =
      header.section_counts[static_cast<std::size_t>(
          PartitionPackageSection::TERMINAL_CAPACITIES)];
  const auto local_to_global = header.section_counts[static_cast<std::size_t>(
      PartitionPackageSection::LOCAL_TO_GLOBAL)];
  const auto reference_labels = header.section_counts[static_cast<std::size_t>(
      PartitionPackageSection::REFERENCE_CUT_LABELS)];
  const auto node_count = static_cast<std::uint64_t>(header.local_node_count);
  require(arcs % 2 == 0,
          "partition package transfer arcs must contain endpoint pairs");
  require(arc_capacities == arcs,
          "partition package transfer arc capacity count mismatch");
  require(terminal_capacities == node_count,
          "partition package transfer terminal capacity count mismatch");
  require(local_to_global == 0 || local_to_global == node_count,
          "partition package transfer local-to-global count mismatch");
  require(reference_labels == 0 || reference_labels == node_count,
          "partition package transfer reference label count mismatch");
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

  template <typename Integer> void writeInteger(const Integer &value) {
    integer_codec::appendSigned(&bytes_, value);
  }

  void writeBool(bool value) { writeU8(value ? 1 : 0); }

  void writeFloat(float value) {
    static_assert(sizeof(float) == sizeof(std::uint32_t),
                  "float serialization expects 32-bit float");
    std::uint32_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU32(bits);
  }

  void writeDouble(double value) {
    static_assert(sizeof(double) == sizeof(std::uint64_t),
                  "double serialization expects 64-bit double");
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    writeU64(bits);
  }

  void writeString(const std::string &value) {
    writeU32(checkedSize(value.size()));
    bytes_.insert(bytes_.end(), value.begin(), value.end());
  }

  template <typename T, typename Container, typename Fn>
  void writeVector(const Container &values, Fn write_one) {
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

  mcpd3::Capacity readCapacity() {
    return integer_codec::capacityFromCppInt(
        integer_codec::readSigned(bytes_, &offset_));
  }

  mcpd3::Objective readObjective() {
    return integer_codec::objectiveFromCppInt(
        integer_codec::readSigned(bytes_, &offset_));
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

  double readDouble() {
    static_assert(sizeof(double) == sizeof(std::uint64_t),
                  "double serialization expects 64-bit double");
    const auto bits = readU64();
    double value = 0.0;
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

void writeAlphaUpdate(Writer *writer, const mcpd3::AlphaUpdate &update) {
  writer->writeI32(update.constraint_id);
  writer->writeInteger(update.alpha);
}

mcpd3::AlphaUpdate readAlphaUpdate(Reader *reader) {
  mcpd3::AlphaUpdate update;
  update.constraint_id = reader->readI32();
  update.alpha = reader->readObjective();
  return update;
}

void writeConstraintEndpoint(Writer *writer,
                             const mcpd3::ConstraintEndpointBinding &binding) {
  writer->writeI32(binding.constraint_id);
  writer->writeI32(binding.global_node_id);
  writer->writeI32(binding.local_index);
  writer->writeBool(binding.is_source);
  writer->writeInteger(binding.alpha);
  writer->writeInteger(binding.last_alpha);
  writer->writeFloat(binding.alpha_momentum);
}

mcpd3::ConstraintEndpointBinding readConstraintEndpoint(Reader *reader) {
  mcpd3::ConstraintEndpointBinding binding;
  binding.constraint_id = reader->readI32();
  binding.global_node_id = reader->readI32();
  binding.local_index = reader->readI32();
  binding.is_source = reader->readBool();
  binding.alpha = reader->readObjective();
  binding.last_alpha = reader->readObjective();
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

void writeNodeLabel(Writer *writer, const mcpd3::NodeLabel &label) {
  writer->writeI32(label.global_node_id);
  writer->writeI32(label.local_index);
  writer->writeI32(label.label);
}

mcpd3::NodeLabel readNodeLabel(Reader *reader) {
  mcpd3::NodeLabel label;
  label.global_node_id = reader->readI32();
  label.local_index = reader->readI32();
  label.label = reader->readI32();
  return label;
}

void writeSolveRoundRequestPayload(
    Writer *writer, const mcpd3::PartitionSolveRequest &message) {
  writer->writeI64(message.round_id);
  writer->writeI32(message.partition_id);
  writer->writeI64(message.scale);
  writer->writeInteger(message.regularization_strength);
  writer->writeBool(message.return_full_labels);
  writer->writeVector<mcpd3::AlphaUpdate>(
      message.alpha_updates,
      [&](const auto &update) { writeAlphaUpdate(writer, update); });
}

mcpd3::PartitionSolveRequest readSolveRoundRequestPayload(Reader *reader) {
  mcpd3::PartitionSolveRequest message;
  message.round_id = checkedIntegerCast<long>(reader->readI64());
  message.partition_id = reader->readI32();
  message.scale = checkedIntegerCast<long>(reader->readI64());
  message.regularization_strength = reader->readCapacity();
  message.return_full_labels = reader->readBool();
  message.alpha_updates = reader->readVector<mcpd3::AlphaUpdate>(
      [&] { return readAlphaUpdate(reader); });
  return message;
}

void writeSolveRoundResultPayload(
    Writer *writer, const mcpd3::PartitionSolveResult &message) {
  writer->writeI64(message.round_id);
  writer->writeI32(message.partition_id);
  writer->writeInteger(message.lower_bound);
  writer->writeInteger(message.regularization_budget);
  writer->writeInteger(message.regularization_contribution);
  writer->writeI64(message.regularization_anchor_sink_count);
  writer->writeI64(message.regularization_active_sink_count);
  writer->writeVector<mcpd3::ConstraintLabel>(
      message.constrained_labels,
      [&](const auto &label) { writeConstraintLabel(writer, label); });
  writer->writeVector<mcpd3::NodeLabel>(
      message.full_labels,
      [&](const auto &label) { writeNodeLabel(writer, label); });
}

mcpd3::PartitionSolveResult readSolveRoundResultPayload(Reader *reader) {
  mcpd3::PartitionSolveResult message;
  message.round_id = checkedIntegerCast<long>(reader->readI64());
  message.partition_id = reader->readI32();
  message.lower_bound = reader->readObjective();
  message.regularization_budget = reader->readObjective();
  message.regularization_contribution = reader->readObjective();
  message.regularization_anchor_sink_count =
      checkedIntegerCast<long>(reader->readI64());
  message.regularization_active_sink_count =
      checkedIntegerCast<long>(reader->readI64());
  message.constrained_labels = reader->readVector<mcpd3::ConstraintLabel>(
      [&] { return readConstraintLabel(reader); });
  message.full_labels =
      reader->readVector<mcpd3::NodeLabel>([&] { return readNodeLabel(reader); });
  return message;
}

void writeDoubles(Writer *writer, const std::vector<double> &values) {
  writer->writeVector<double>(values,
                              [&](double value) { writer->writeDouble(value); });
}

std::vector<double> readDoubles(Reader *reader) {
  return reader->readVector<double>([&] { return reader->readDouble(); });
}

void writeLinearVectorRequest(Writer *writer,
                              const LinearVectorRequest &message) {
  writer->writeI32(message.partition_id);
  writeDoubles(writer, message.values);
}

LinearVectorRequest readLinearVectorRequest(Reader *reader) {
  LinearVectorRequest message;
  message.partition_id = reader->readI32();
  message.values = readDoubles(reader);
  return message;
}

void writeLinearScalarRequest(Writer *writer,
                              const LinearScalarRequest &message) {
  writer->writeI32(message.partition_id);
  writer->writeDouble(message.value);
}

LinearScalarRequest readLinearScalarRequest(Reader *reader) {
  LinearScalarRequest message;
  message.partition_id = reader->readI32();
  message.value = reader->readDouble();
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
  Frame frame;
  frame.type = static_cast<MessageType>(raw_type);
  frame.payload.assign(bytes.begin() +
                           static_cast<std::ptrdiff_t>(kFrameHeaderBytes),
                       bytes.end());
  return frame;
}

std::vector<std::uint8_t> encodeHello(const HelloMessage &message) {
  Writer writer;
  writer.writeU32(message.protocol_version);
  writer.writeU32(static_cast<std::uint32_t>(message.capacity_mode));
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
  const auto capacity_mode = reader.readU32();
  require(isKnownCapacityMode(capacity_mode), "unknown capacity mode");
  message.capacity_mode = static_cast<CapacityMode>(capacity_mode);
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
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeI32(message.local_node_count);
  writer.writeVector<int>(message.arcs,
                          [&](int value) { writer.writeI32(value); });
  writer.writeVector<mcpd3::Capacity>(
      message.arc_capacities,
      [&](const auto &value) { writer.writeInteger(value); });
  writer.writeVector<mcpd3::Capacity>(
      message.terminal_capacities,
      [&](const auto &value) { writer.writeInteger(value); });
  writer.writeVector<int>(message.local_to_global,
                          [&](int value) { writer.writeI32(value); });
  writer.writeVector<mcpd3::ConstraintEndpointBinding>(
      message.constraint_endpoints,
      [&](const auto &binding) { writeConstraintEndpoint(&writer, binding); });
  writer.writeI64(message.objective_multiplier);
  writer.writeU32(
      static_cast<std::uint32_t>(message.canonical_cut_selection));
  writer.writeBool(message.force_full_mincut_recompute);
  writer.writeVector<int>(message.reference_cut_labels,
                          [&](int value) { writer.writeI32(value); });
  writer.writeU32(
      static_cast<std::uint32_t>(message.reference_cut_selection));
  writer.writeI64(message.reference_cut_check_interval);
  return encodeFrame(MessageType::PARTITION_PACKAGE, writer.bytes());
}

mcpd3::PartitionPackage decodePartitionPackage(
    const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::PARTITION_PACKAGE);
  Reader reader(decoded.payload);
  mcpd3::PartitionPackage message;
  message.partition_id = reader.readI32();
  message.local_node_count = reader.readI32();
  message.arcs = reader.readVector<int>([&] { return reader.readI32(); });
  message.arc_capacities = reader.readVector<mcpd3::Capacity>(
      [&] { return reader.readCapacity(); });
  message.terminal_capacities = reader.readVector<mcpd3::Capacity>(
      [&] { return reader.readCapacity(); });
  message.local_to_global =
      reader.readVector<int>([&] { return reader.readI32(); });
  message.constraint_endpoints =
      reader.readVector<mcpd3::ConstraintEndpointBinding>(
          [&] { return readConstraintEndpoint(&reader); });
  message.objective_multiplier =
      checkedIntegerCast<long>(reader.readI64());
  const std::uint32_t canonical_selection = reader.readU32();
  require(isKnownCanonicalCutSelection(canonical_selection),
          "unknown canonical cut selection");
  message.canonical_cut_selection =
      static_cast<mcpd3::CanonicalCutSelection>(canonical_selection);
  message.force_full_mincut_recompute = reader.readBool();
  message.reference_cut_labels =
      reader.readVector<int>([&] { return reader.readI32(); });
  const std::uint32_t reference_selection = reader.readU32();
  require(isKnownReferenceCutSelection(reference_selection),
          "unknown reference cut selection");
  message.reference_cut_selection =
      static_cast<mcpd3::ReferenceCutSelection>(reference_selection);
  message.reference_cut_check_interval =
      checkedIntegerCast<long>(reader.readI64());
  requireDone(reader);
  return message;
}

std::size_t PartitionPackageTransferChunk::size() const {
  switch (section) {
  case PartitionPackageSection::ARCS:
  case PartitionPackageSection::LOCAL_TO_GLOBAL:
  case PartitionPackageSection::REFERENCE_CUT_LABELS:
    return int_values.size();
  case PartitionPackageSection::ARC_CAPACITIES:
  case PartitionPackageSection::TERMINAL_CAPACITIES:
    return capacity_values.size();
  case PartitionPackageSection::CONSTRAINT_ENDPOINTS:
    return constraint_values.size();
  }
  throw std::runtime_error("unknown partition package section");
}

PartitionPackageTransferHeader makePartitionPackageTransferHeader(
    const mcpd3::PartitionPackage &message) {
  PartitionPackageTransferHeader header;
  header.partition_id = message.partition_id;
  header.local_node_count = message.local_node_count;
  header.section_counts = {
      static_cast<std::uint64_t>(message.arcs.size()),
      static_cast<std::uint64_t>(message.arc_capacities.size()),
      static_cast<std::uint64_t>(message.terminal_capacities.size()),
      static_cast<std::uint64_t>(message.local_to_global.size()),
      static_cast<std::uint64_t>(message.constraint_endpoints.size()),
      static_cast<std::uint64_t>(message.reference_cut_labels.size())};
  header.objective_multiplier = message.objective_multiplier;
  header.canonical_cut_selection = message.canonical_cut_selection;
  header.force_full_mincut_recompute = message.force_full_mincut_recompute;
  header.reference_cut_selection = message.reference_cut_selection;
  header.reference_cut_check_interval = message.reference_cut_check_interval;
  return header;
}

std::vector<std::uint8_t> encodePartitionPackageTransferBegin(
    const PartitionPackageTransferHeader &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeI32(message.local_node_count);
  for (const auto count : message.section_counts) {
    writer.writeU64(count);
  }
  writer.writeI64(message.objective_multiplier);
  writer.writeU32(
      static_cast<std::uint32_t>(message.canonical_cut_selection));
  writer.writeBool(message.force_full_mincut_recompute);
  writer.writeU32(
      static_cast<std::uint32_t>(message.reference_cut_selection));
  writer.writeI64(message.reference_cut_check_interval);
  return encodeFrame(MessageType::PARTITION_PACKAGE_BEGIN, writer.bytes());
}

PartitionPackageTransferHeader decodePartitionPackageTransferBegin(
    const std::vector<std::uint8_t> &frame) {
  auto decoded =
      decodeExpectedFrame(frame, MessageType::PARTITION_PACKAGE_BEGIN);
  Reader reader(decoded.payload);
  PartitionPackageTransferHeader message;
  message.partition_id = reader.readI32();
  message.local_node_count = reader.readI32();
  for (auto &count : message.section_counts) {
    count = reader.readU64();
    (void)checkedContainerSize(count);
  }
  message.objective_multiplier =
      checkedIntegerCast<long>(reader.readI64());
  const auto canonical_selection = reader.readU32();
  require(isKnownCanonicalCutSelection(canonical_selection),
          "unknown canonical cut selection");
  message.canonical_cut_selection =
      static_cast<mcpd3::CanonicalCutSelection>(canonical_selection);
  message.force_full_mincut_recompute = reader.readBool();
  const auto reference_selection = reader.readU32();
  require(isKnownReferenceCutSelection(reference_selection),
          "unknown reference cut selection");
  message.reference_cut_selection =
      static_cast<mcpd3::ReferenceCutSelection>(reference_selection);
  message.reference_cut_check_interval =
      checkedIntegerCast<long>(reader.readI64());
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodePartitionPackageTransferChunk(
    const mcpd3::PartitionPackage &message, PartitionPackageSection section,
    std::uint64_t offset, std::size_t count) {
  const auto header = makePartitionPackageTransferHeader(message);
  const auto section_index = partitionPackageSectionIndex(section);
  const auto total = header.section_counts[section_index];
  require(offset <= total && count <= total - offset,
          "partition package chunk is outside its section");
  const auto begin = checkedContainerSize(offset);
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeU32(static_cast<std::uint32_t>(section));
  writer.writeU64(offset);
  writer.writeU32(checkedSize(count));
  for (std::size_t i = 0; i < count; ++i) {
    const auto index = begin + i;
    switch (section) {
    case PartitionPackageSection::ARCS:
      writer.writeI32(message.arcs[index]);
      break;
    case PartitionPackageSection::ARC_CAPACITIES:
      writer.writeInteger(message.arc_capacities[index]);
      break;
    case PartitionPackageSection::TERMINAL_CAPACITIES:
      writer.writeInteger(message.terminal_capacities[index]);
      break;
    case PartitionPackageSection::LOCAL_TO_GLOBAL:
      writer.writeI32(message.local_to_global[index]);
      break;
    case PartitionPackageSection::CONSTRAINT_ENDPOINTS:
      writeConstraintEndpoint(&writer, message.constraint_endpoints[index]);
      break;
    case PartitionPackageSection::REFERENCE_CUT_LABELS:
      writer.writeI32(message.reference_cut_labels[index]);
      break;
    }
  }
  return encodeFrame(MessageType::PARTITION_PACKAGE_CHUNK, writer.bytes());
}

PartitionPackageTransferChunk decodePartitionPackageTransferChunk(
    const std::vector<std::uint8_t> &frame) {
  auto decoded =
      decodeExpectedFrame(frame, MessageType::PARTITION_PACKAGE_CHUNK);
  Reader reader(decoded.payload);
  PartitionPackageTransferChunk message;
  message.partition_id = reader.readI32();
  const auto section = reader.readU32();
  require(isKnownPartitionPackageSection(section),
          "unknown partition package section");
  message.section = static_cast<PartitionPackageSection>(section);
  message.offset = reader.readU64();
  const auto count = reader.readU32();
  switch (message.section) {
  case PartitionPackageSection::ARCS:
  case PartitionPackageSection::LOCAL_TO_GLOBAL:
  case PartitionPackageSection::REFERENCE_CUT_LABELS:
    message.int_values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      message.int_values.push_back(reader.readI32());
    }
    break;
  case PartitionPackageSection::ARC_CAPACITIES:
  case PartitionPackageSection::TERMINAL_CAPACITIES:
    message.capacity_values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      message.capacity_values.push_back(reader.readCapacity());
    }
    break;
  case PartitionPackageSection::CONSTRAINT_ENDPOINTS:
    message.constraint_values.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
      message.constraint_values.push_back(readConstraintEndpoint(&reader));
    }
    break;
  }
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodePartitionPackageTransferEnd(
    const PartitionPackageTransferEnd &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  return encodeFrame(MessageType::PARTITION_PACKAGE_END, writer.bytes());
}

PartitionPackageTransferEnd decodePartitionPackageTransferEnd(
    const std::vector<std::uint8_t> &frame) {
  auto decoded =
      decodeExpectedFrame(frame, MessageType::PARTITION_PACKAGE_END);
  Reader reader(decoded.payload);
  PartitionPackageTransferEnd message;
  message.partition_id = reader.readI32();
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeFullLabelsRequest(
    const FullLabelsRequest &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeU64(message.offset);
  writer.writeU64(message.count);
  return encodeFrame(MessageType::FULL_LABELS_REQUEST, writer.bytes());
}

FullLabelsRequest decodeFullLabelsRequest(
    const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::FULL_LABELS_REQUEST);
  Reader reader(decoded.payload);
  FullLabelsRequest message;
  message.partition_id = reader.readI32();
  message.offset = reader.readU64();
  message.count = reader.readU64();
  requireDone(reader);
  require(message.partition_id >= 0,
          "full label request partition id must be non-negative");
  (void)checkedContainerSize(message.offset);
  (void)checkedContainerSize(message.count);
  return message;
}

std::vector<std::uint8_t> encodeFullLabelsChunk(
    const FullLabelsChunk &message) {
  require(!message.labels.empty(), "full label chunk must not be empty");
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeU64(message.offset);
  writer.writeVector<mcpd3::NodeLabel>(
      message.labels, [&](const auto &label) {
        require(label.label == 0 || label.label == 1,
                "full label chunk contains a non-binary label");
        writeNodeLabel(&writer, label);
      });
  return encodeFrame(MessageType::FULL_LABELS_CHUNK, writer.bytes());
}

FullLabelsChunk decodeFullLabelsChunk(
    const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::FULL_LABELS_CHUNK);
  Reader reader(decoded.payload);
  FullLabelsChunk message;
  message.partition_id = reader.readI32();
  message.offset = reader.readU64();
  message.labels = reader.readVector<mcpd3::NodeLabel>([&] {
    auto label = readNodeLabel(&reader);
    require(label.label == 0 || label.label == 1,
            "full label chunk contains a non-binary label");
    return label;
  });
  requireDone(reader);
  require(message.partition_id >= 0,
          "full label chunk partition id must be non-negative");
  require(!message.labels.empty(), "full label chunk must not be empty");
  return message;
}

std::vector<std::uint8_t> encodeFullLabelsEnd(
    const FullLabelsEnd &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  return encodeFrame(MessageType::FULL_LABELS_END, writer.bytes());
}

FullLabelsEnd decodeFullLabelsEnd(const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::FULL_LABELS_END);
  Reader reader(decoded.payload);
  FullLabelsEnd message;
  message.partition_id = reader.readI32();
  requireDone(reader);
  require(message.partition_id >= 0,
          "full label end partition id must be non-negative");
  return message;
}

PartitionPackageAssembler::PartitionPackageAssembler(
    PartitionPackageTransferHeader header,
    mcpd3::SolverStorageOptions storage)
    : header_(std::move(header)) {
  validatePartitionPackageTransferHeader(header_);
  package_.partition_id = header_.partition_id;
  package_.local_node_count = header_.local_node_count;
  package_.arcs = mcpd3::SolverArray<int>(
      checkedContainerSize(header_.section_counts[0]), storage,
      "transport_partition_arcs");
  package_.arc_capacities = mcpd3::SolverArray<mcpd3::Capacity>(
      checkedContainerSize(header_.section_counts[1]), storage,
      "transport_partition_arc_capacities");
  package_.terminal_capacities = mcpd3::SolverArray<mcpd3::Capacity>(
      checkedContainerSize(header_.section_counts[2]), storage,
      "transport_partition_terminal_capacities");
  package_.local_to_global = mcpd3::SolverArray<int>(
      checkedContainerSize(header_.section_counts[3]), storage,
      "transport_partition_local_to_global");
  package_.constraint_endpoints.resize(
      checkedContainerSize(header_.section_counts[4]));
  package_.reference_cut_labels = mcpd3::SolverArray<int>(
      checkedContainerSize(header_.section_counts[5]), storage,
      "transport_partition_reference_labels");
  package_.objective_multiplier = header_.objective_multiplier;
  package_.canonical_cut_selection = header_.canonical_cut_selection;
  package_.force_full_mincut_recompute =
      header_.force_full_mincut_recompute;
  package_.reference_cut_selection = header_.reference_cut_selection;
  package_.reference_cut_check_interval =
      header_.reference_cut_check_interval;
}

void PartitionPackageAssembler::append(PartitionPackageTransferChunk chunk) {
  if (finished_) {
    throw std::runtime_error("partition package transfer is already finished");
  }
  if (chunk.partition_id != header_.partition_id) {
    throw std::runtime_error("partition package chunk id mismatch");
  }
  const auto section_index = partitionPackageSectionIndex(chunk.section);
  if (chunk.offset != next_offsets_[section_index]) {
    throw std::runtime_error("partition package chunk offset is not contiguous");
  }
  const auto count = static_cast<std::uint64_t>(chunk.size());
  if (count == 0) {
    throw std::runtime_error("partition package chunk must not be empty");
  }
  const auto total = header_.section_counts[section_index];
  if (count > total - chunk.offset) {
    throw std::runtime_error("partition package chunk exceeds section size");
  }
  const auto offset = checkedContainerSize(chunk.offset);
  switch (chunk.section) {
  case PartitionPackageSection::ARCS:
    if (!chunk.capacity_values.empty() || !chunk.constraint_values.empty()) {
      throw std::runtime_error("partition package chunk value type mismatch");
    }
    std::copy(chunk.int_values.begin(), chunk.int_values.end(),
              package_.arcs.begin() + offset);
    break;
  case PartitionPackageSection::ARC_CAPACITIES:
    if (!chunk.int_values.empty() || !chunk.constraint_values.empty()) {
      throw std::runtime_error("partition package chunk value type mismatch");
    }
    std::copy(chunk.capacity_values.begin(), chunk.capacity_values.end(),
              package_.arc_capacities.begin() + offset);
    break;
  case PartitionPackageSection::TERMINAL_CAPACITIES:
    if (!chunk.int_values.empty() || !chunk.constraint_values.empty()) {
      throw std::runtime_error("partition package chunk value type mismatch");
    }
    std::copy(chunk.capacity_values.begin(), chunk.capacity_values.end(),
              package_.terminal_capacities.begin() + offset);
    break;
  case PartitionPackageSection::LOCAL_TO_GLOBAL:
    if (!chunk.capacity_values.empty() || !chunk.constraint_values.empty()) {
      throw std::runtime_error("partition package chunk value type mismatch");
    }
    std::copy(chunk.int_values.begin(), chunk.int_values.end(),
              package_.local_to_global.begin() + offset);
    break;
  case PartitionPackageSection::CONSTRAINT_ENDPOINTS:
    if (!chunk.int_values.empty() || !chunk.capacity_values.empty()) {
      throw std::runtime_error("partition package chunk value type mismatch");
    }
    std::copy(chunk.constraint_values.begin(), chunk.constraint_values.end(),
              package_.constraint_endpoints.begin() + offset);
    break;
  case PartitionPackageSection::REFERENCE_CUT_LABELS:
    if (!chunk.capacity_values.empty() || !chunk.constraint_values.empty()) {
      throw std::runtime_error("partition package chunk value type mismatch");
    }
    std::copy(chunk.int_values.begin(), chunk.int_values.end(),
              package_.reference_cut_labels.begin() + offset);
    break;
  }
  next_offsets_[section_index] += count;
}

bool PartitionPackageAssembler::complete() const {
  return next_offsets_ == header_.section_counts;
}

mcpd3::PartitionPackage PartitionPackageAssembler::finish() {
  if (finished_) {
    throw std::runtime_error("partition package transfer is already finished");
  }
  if (!complete()) {
    throw std::runtime_error("partition package transfer is incomplete");
  }
  mcpd3::validatePartitionPackage(package_);
  finished_ = true;
  return std::move(package_);
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
  writer.writeVector<int>(message.partition_ids,
                          [&](int value) { writer.writeI32(value); });
  writer.writeI64(message.factor);
  writer.writeBool(message.saturate_capacity_overflow);
  return encodeFrame(MessageType::SCALE_OBJECTIVE, writer.bytes());
}

ScaleObjectiveMessage decodeScaleObjective(
    const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(frame, MessageType::SCALE_OBJECTIVE);
  Reader reader(decoded.payload);
  ScaleObjectiveMessage message;
  message.partition_ids =
      reader.readVector<int>([&] { return reader.readI32(); });
  message.factor = reader.readI64();
  message.saturate_capacity_overflow = reader.readBool();
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodePartitionCapacityUpdate(
    const mcpd3::PartitionCapacityUpdate &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeVector<mcpd3::Capacity>(
      message.arc_capacities,
      [&](const auto &value) { writer.writeInteger(value); });
  writer.writeVector<mcpd3::Capacity>(
      message.terminal_capacities,
      [&](const auto &value) { writer.writeInteger(value); });
  writer.writeBool(message.preserve_flow_state);
  writer.writeInteger(message.flow_scale_numerator);
  writer.writeInteger(message.flow_scale_denominator);
  return encodeFrame(MessageType::REPLACE_PARTITION_CAPACITIES,
                     writer.bytes());
}

mcpd3::PartitionCapacityUpdate decodePartitionCapacityUpdate(
    const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(
      frame, MessageType::REPLACE_PARTITION_CAPACITIES);
  Reader reader(decoded.payload);
  mcpd3::PartitionCapacityUpdate message;
  message.partition_id = reader.readI32();
  message.arc_capacities = reader.readVector<mcpd3::Capacity>(
      [&] { return reader.readCapacity(); });
  message.terminal_capacities = reader.readVector<mcpd3::Capacity>(
      [&] { return reader.readCapacity(); });
  message.preserve_flow_state = reader.readBool();
  message.flow_scale_numerator = reader.readObjective();
  message.flow_scale_denominator = reader.readObjective();
  requireDone(reader);
  return message;
}

PartitionCapacityUpdateTransferHeader makePartitionCapacityUpdateTransferHeader(
    const mcpd3::PartitionCapacityUpdate &message) {
  PartitionCapacityUpdateTransferHeader header;
  header.partition_id = message.partition_id;
  header.section_counts = {
      static_cast<std::uint64_t>(message.arc_capacities.size()),
      static_cast<std::uint64_t>(message.terminal_capacities.size())};
  header.preserve_flow_state = message.preserve_flow_state;
  header.flow_scale_numerator = message.flow_scale_numerator;
  header.flow_scale_denominator = message.flow_scale_denominator;
  return header;
}

std::vector<std::uint8_t> encodePartitionCapacityUpdateTransferBegin(
    const PartitionCapacityUpdateTransferHeader &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  for (const auto count : message.section_counts) {
    writer.writeU64(count);
  }
  writer.writeBool(message.preserve_flow_state);
  writer.writeInteger(message.flow_scale_numerator);
  writer.writeInteger(message.flow_scale_denominator);
  return encodeFrame(MessageType::PARTITION_CAPACITY_UPDATE_BEGIN,
                     writer.bytes());
}

PartitionCapacityUpdateTransferHeader decodePartitionCapacityUpdateTransferBegin(
    const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(
      frame, MessageType::PARTITION_CAPACITY_UPDATE_BEGIN);
  Reader reader(decoded.payload);
  PartitionCapacityUpdateTransferHeader message;
  message.partition_id = reader.readI32();
  for (auto &count : message.section_counts) {
    count = reader.readU64();
    (void)checkedContainerSize(count);
  }
  message.preserve_flow_state = reader.readBool();
  message.flow_scale_numerator = reader.readObjective();
  message.flow_scale_denominator = reader.readObjective();
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodePartitionCapacityUpdateTransferChunk(
    const mcpd3::PartitionCapacityUpdate &message,
    PartitionCapacityUpdateSection section, std::uint64_t offset,
    std::size_t count) {
  const auto header = makePartitionCapacityUpdateTransferHeader(message);
  const auto section_index = partitionCapacityUpdateSectionIndex(section);
  const auto total = header.section_counts[section_index];
  require(offset <= total && count <= total - offset,
          "partition capacity update chunk is outside its section");
  const auto begin = checkedContainerSize(offset);
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeU32(static_cast<std::uint32_t>(section));
  writer.writeU64(offset);
  writer.writeU32(checkedSize(count));
  for (std::size_t i = 0; i < count; ++i) {
    const auto index = begin + i;
    switch (section) {
    case PartitionCapacityUpdateSection::ARC_CAPACITIES:
      writer.writeInteger(message.arc_capacities[index]);
      break;
    case PartitionCapacityUpdateSection::TERMINAL_CAPACITIES:
      writer.writeInteger(message.terminal_capacities[index]);
      break;
    }
  }
  return encodeFrame(MessageType::PARTITION_CAPACITY_UPDATE_CHUNK,
                     writer.bytes());
}

PartitionCapacityUpdateTransferChunk decodePartitionCapacityUpdateTransferChunk(
    const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(
      frame, MessageType::PARTITION_CAPACITY_UPDATE_CHUNK);
  Reader reader(decoded.payload);
  PartitionCapacityUpdateTransferChunk message;
  message.partition_id = reader.readI32();
  const auto section = reader.readU32();
  require(isKnownPartitionCapacityUpdateSection(section),
          "unknown partition capacity update section");
  message.section = static_cast<PartitionCapacityUpdateSection>(section);
  message.offset = reader.readU64();
  const auto count = reader.readU32();
  message.values.reserve(count);
  for (std::uint32_t i = 0; i < count; ++i) {
    message.values.push_back(reader.readCapacity());
  }
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodePartitionCapacityUpdateTransferEnd(
    const PartitionCapacityUpdateTransferEnd &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  return encodeFrame(MessageType::PARTITION_CAPACITY_UPDATE_END,
                     writer.bytes());
}

PartitionCapacityUpdateTransferEnd decodePartitionCapacityUpdateTransferEnd(
    const std::vector<std::uint8_t> &frame) {
  auto decoded = decodeExpectedFrame(
      frame, MessageType::PARTITION_CAPACITY_UPDATE_END);
  Reader reader(decoded.payload);
  PartitionCapacityUpdateTransferEnd message;
  message.partition_id = reader.readI32();
  requireDone(reader);
  return message;
}

PartitionCapacityUpdateAssembler::PartitionCapacityUpdateAssembler(
    PartitionCapacityUpdateTransferHeader header,
    mcpd3::SolverStorageOptions storage)
    : header_(std::move(header)) {
  require(header_.partition_id >= 0,
          "partition capacity update id must be non-negative");
  require(header_.flow_scale_numerator > 0,
          "partition capacity update flow numerator must be positive");
  require(header_.flow_scale_denominator > 0,
          "partition capacity update flow denominator must be positive");
  update_.partition_id = header_.partition_id;
  update_.arc_capacities = mcpd3::SolverArray<mcpd3::Capacity>(
      checkedContainerSize(header_.section_counts[0]), storage,
      "transport_update_arc_capacities");
  update_.terminal_capacities = mcpd3::SolverArray<mcpd3::Capacity>(
      checkedContainerSize(header_.section_counts[1]), storage,
      "transport_update_terminal_capacities");
  update_.preserve_flow_state = header_.preserve_flow_state;
  update_.flow_scale_numerator = header_.flow_scale_numerator;
  update_.flow_scale_denominator = header_.flow_scale_denominator;
}

void PartitionCapacityUpdateAssembler::append(
    PartitionCapacityUpdateTransferChunk chunk) {
  if (finished_) {
    throw std::runtime_error(
        "partition capacity update transfer is already finished");
  }
  if (chunk.partition_id != header_.partition_id) {
    throw std::runtime_error("partition capacity update chunk id mismatch");
  }
  const auto section_index =
      partitionCapacityUpdateSectionIndex(chunk.section);
  if (chunk.offset != next_offsets_[section_index]) {
    throw std::runtime_error(
        "partition capacity update chunk offset is not contiguous");
  }
  if (chunk.values.empty()) {
    throw std::runtime_error(
        "partition capacity update chunk must not be empty");
  }
  const auto count = static_cast<std::uint64_t>(chunk.values.size());
  const auto total = header_.section_counts[section_index];
  if (count > total - chunk.offset) {
    throw std::runtime_error(
        "partition capacity update chunk exceeds section size");
  }
  const auto offset = checkedContainerSize(chunk.offset);
  switch (chunk.section) {
  case PartitionCapacityUpdateSection::ARC_CAPACITIES:
    std::copy(chunk.values.begin(), chunk.values.end(),
              update_.arc_capacities.begin() + offset);
    break;
  case PartitionCapacityUpdateSection::TERMINAL_CAPACITIES:
    std::copy(chunk.values.begin(), chunk.values.end(),
              update_.terminal_capacities.begin() + offset);
    break;
  }
  next_offsets_[section_index] += count;
}

bool PartitionCapacityUpdateAssembler::complete() const {
  return next_offsets_ == header_.section_counts;
}

mcpd3::PartitionCapacityUpdate PartitionCapacityUpdateAssembler::finish() {
  if (finished_) {
    throw std::runtime_error(
        "partition capacity update transfer is already finished");
  }
  if (!complete()) {
    throw std::runtime_error(
        "partition capacity update transfer is incomplete");
  }
  finished_ = true;
  return std::move(update_);
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

std::vector<std::uint8_t> encodeLinearStructure(
    const LinearStructureMessage &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeVector<int>(message.owned_global_nodes,
                          [&](int value) { writer.writeI32(value); });
  writer.writeVector<int>(message.ghost_global_nodes,
                          [&](int value) { writer.writeI32(value); });
  writer.writeVector<std::uint64_t>(
      message.row_offsets,
      [&](std::uint64_t value) { writer.writeU64(value); });
  writer.writeVector<int>(message.column_indices,
                          [&](int value) { writer.writeI32(value); });
  writer.writeVector<int>(message.boundary_owned_local_indices,
                          [&](int value) { writer.writeI32(value); });
  return encodeFrame(MessageType::LINEAR_STRUCTURE, writer.bytes());
}

LinearStructureMessage decodeLinearStructure(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_STRUCTURE);
  Reader reader(decoded.payload);
  LinearStructureMessage message;
  message.partition_id = reader.readI32();
  message.owned_global_nodes =
      reader.readVector<int>([&] { return reader.readI32(); });
  message.ghost_global_nodes =
      reader.readVector<int>([&] { return reader.readI32(); });
  message.row_offsets = reader.readVector<std::uint64_t>(
      [&] { return reader.readU64(); });
  message.column_indices =
      reader.readVector<int>([&] { return reader.readI32(); });
  message.boundary_owned_local_indices =
      reader.readVector<int>([&] { return reader.readI32(); });
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeLinearSystemValues(
    const LinearSystemValuesMessage &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  writeDoubles(&writer, message.values);
  writeDoubles(&writer, message.rhs);
  writeDoubles(&writer, message.initial_x);
  return encodeFrame(MessageType::LINEAR_SYSTEM_VALUES, writer.bytes());
}

LinearSystemValuesMessage decodeLinearSystemValues(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_SYSTEM_VALUES);
  Reader reader(decoded.payload);
  LinearSystemValuesMessage message;
  message.partition_id = reader.readI32();
  message.values = readDoubles(&reader);
  message.rhs = readDoubles(&reader);
  message.initial_x = readDoubles(&reader);
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeLinearInitializeRequest(
    const LinearVectorRequest &message) {
  Writer writer;
  writeLinearVectorRequest(&writer, message);
  return encodeFrame(MessageType::LINEAR_INITIALIZE_REQUEST, writer.bytes());
}

LinearVectorRequest decodeLinearInitializeRequest(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_INITIALIZE_REQUEST);
  Reader reader(decoded.payload);
  auto message = readLinearVectorRequest(&reader);
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeLinearInitializeResult(
    const LinearInitializeResultMessage &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeDouble(message.result.rhs_norm_squared);
  writer.writeDouble(message.result.residual_norm_squared);
  writer.writeDouble(message.result.residual_preconditioned_inner);
  writeDoubles(&writer, message.result.boundary_direction);
  return encodeFrame(MessageType::LINEAR_INITIALIZE_RESULT, writer.bytes());
}

LinearInitializeResultMessage decodeLinearInitializeResult(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_INITIALIZE_RESULT);
  Reader reader(decoded.payload);
  LinearInitializeResultMessage message;
  message.partition_id = reader.readI32();
  message.result.rhs_norm_squared = reader.readDouble();
  message.result.residual_norm_squared = reader.readDouble();
  message.result.residual_preconditioned_inner = reader.readDouble();
  message.result.boundary_direction = readDoubles(&reader);
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeLinearMultiplyRequest(
    const LinearVectorRequest &message) {
  Writer writer;
  writeLinearVectorRequest(&writer, message);
  return encodeFrame(MessageType::LINEAR_MULTIPLY_REQUEST, writer.bytes());
}

LinearVectorRequest decodeLinearMultiplyRequest(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_MULTIPLY_REQUEST);
  Reader reader(decoded.payload);
  auto message = readLinearVectorRequest(&reader);
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeLinearMultiplyResult(
    const LinearMultiplyResultMessage &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeDouble(message.result.direction_product_inner);
  return encodeFrame(MessageType::LINEAR_MULTIPLY_RESULT, writer.bytes());
}

LinearMultiplyResultMessage decodeLinearMultiplyResult(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_MULTIPLY_RESULT);
  Reader reader(decoded.payload);
  LinearMultiplyResultMessage message;
  message.partition_id = reader.readI32();
  message.result.direction_product_inner = reader.readDouble();
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeLinearAlphaRequest(
    const LinearScalarRequest &message) {
  Writer writer;
  writeLinearScalarRequest(&writer, message);
  return encodeFrame(MessageType::LINEAR_ALPHA_REQUEST, writer.bytes());
}

LinearScalarRequest decodeLinearAlphaRequest(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_ALPHA_REQUEST);
  Reader reader(decoded.payload);
  auto message = readLinearScalarRequest(&reader);
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeLinearAlphaResult(
    const LinearAlphaResultMessage &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  writer.writeDouble(message.result.residual_norm_squared);
  writer.writeDouble(message.result.residual_preconditioned_inner);
  return encodeFrame(MessageType::LINEAR_ALPHA_RESULT, writer.bytes());
}

LinearAlphaResultMessage decodeLinearAlphaResult(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_ALPHA_RESULT);
  Reader reader(decoded.payload);
  LinearAlphaResultMessage message;
  message.partition_id = reader.readI32();
  message.result.residual_norm_squared = reader.readDouble();
  message.result.residual_preconditioned_inner = reader.readDouble();
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeLinearBetaRequest(
    const LinearScalarRequest &message) {
  Writer writer;
  writeLinearScalarRequest(&writer, message);
  return encodeFrame(MessageType::LINEAR_BETA_REQUEST, writer.bytes());
}

LinearScalarRequest decodeLinearBetaRequest(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_BETA_REQUEST);
  Reader reader(decoded.payload);
  auto message = readLinearScalarRequest(&reader);
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeLinearBetaResult(
    const LinearBetaResultMessage &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  writeDoubles(&writer, message.result.boundary_direction);
  return encodeFrame(MessageType::LINEAR_BETA_RESULT, writer.bytes());
}

LinearBetaResultMessage decodeLinearBetaResult(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_BETA_RESULT);
  Reader reader(decoded.payload);
  LinearBetaResultMessage message;
  message.partition_id = reader.readI32();
  message.result.boundary_direction = readDoubles(&reader);
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeLinearSolutionRequest(
    const LinearPartitionRequest &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  return encodeFrame(MessageType::LINEAR_SOLUTION_REQUEST, writer.bytes());
}

LinearPartitionRequest decodeLinearSolutionRequest(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_SOLUTION_REQUEST);
  Reader reader(decoded.payload);
  LinearPartitionRequest message;
  message.partition_id = reader.readI32();
  requireDone(reader);
  return message;
}

std::vector<std::uint8_t> encodeLinearSolutionResult(
    const LinearSolutionResultMessage &message) {
  Writer writer;
  writer.writeI32(message.partition_id);
  writeDoubles(&writer, message.solution);
  return encodeFrame(MessageType::LINEAR_SOLUTION_RESULT, writer.bytes());
}

LinearSolutionResultMessage decodeLinearSolutionResult(
    const std::vector<std::uint8_t> &frame) {
  const auto decoded =
      decodeExpectedFrame(frame, MessageType::LINEAR_SOLUTION_RESULT);
  Reader reader(decoded.payload);
  LinearSolutionResultMessage message;
  message.partition_id = reader.readI32();
  message.solution = readDoubles(&reader);
  requireDone(reader);
  return message;
}

} // namespace mcpd4
