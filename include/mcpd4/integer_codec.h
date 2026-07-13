#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

#include <boost/multiprecision/cpp_int.hpp>

#include <capacity.h>

namespace mcpd4::integer_codec {

template <typename Integer>
inline boost::multiprecision::cpp_int toCppInt(const Integer &value) {
  return boost::multiprecision::cpp_int(mcpd3::integer_to_string(value));
}

template <typename Integer>
inline void appendSigned(std::vector<std::uint8_t> *bytes,
                         const Integer &value) {
  auto signed_value = toCppInt(value);
  boost::multiprecision::cpp_int encoded;
  if (signed_value < 0) {
    encoded = (-signed_value) * 2 - 1;
  } else {
    encoded = signed_value * 2;
  }
  do {
    const auto payload = static_cast<std::uint8_t>(
        (encoded & 0x7f).template convert_to<unsigned>());
    encoded >>= 7;
    bytes->push_back(encoded == 0 ? payload
                                  : static_cast<std::uint8_t>(payload | 0x80));
  } while (encoded != 0);
}

inline boost::multiprecision::cpp_int
readSigned(const std::vector<std::uint8_t> &bytes, std::size_t *offset) {
  boost::multiprecision::cpp_int encoded = 0;
  unsigned shift = 0;
  std::size_t count = 0;
  while (true) {
    if (*offset >= bytes.size()) {
      throw std::runtime_error("truncated signed integer varint");
    }
    const std::uint8_t byte = bytes[(*offset)++];
    const std::uint8_t payload = byte & 0x7f;
    encoded |= boost::multiprecision::cpp_int(payload) << shift;
    ++count;
    if ((byte & 0x80) == 0) {
      if (count > 1 && payload == 0) {
        throw std::runtime_error("non-canonical signed integer varint");
      }
      break;
    }
    shift += 7;
  }
  boost::multiprecision::cpp_int decoded;
  if ((encoded & 1) != 0) {
    decoded = -((encoded + 1) >> 1);
  } else {
    decoded = encoded >> 1;
  }
  return decoded;
}

inline mcpd3::Capacity capacityFromCppInt(
    const boost::multiprecision::cpp_int &value) {
  return mcpd3::parse_capacity(value.convert_to<std::string>());
}

inline mcpd3::Objective objectiveFromCppInt(
    const boost::multiprecision::cpp_int &value) {
  return mcpd3::parse_objective(value.convert_to<std::string>());
}

} // namespace mcpd4::integer_codec
