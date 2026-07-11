#pragma once

#include <cstddef>
#include <cstdint>

namespace mcpd4 {

struct ByteBufferView {
  const std::uint8_t *data = nullptr;
  std::size_t size = 0;
};

} // namespace mcpd4
