#include "kestral/search/vbyte.hpp"

namespace kestral {

void encode_vbyte(std::uint32_t value, std::vector<std::uint8_t> &out) {
  while (value >= 128) {
    out.push_back(static_cast<std::uint8_t>((value & 127) | 128));
    value >>= 7;
  }
  out.push_back(static_cast<std::uint8_t>(value & 127));
}

const std::uint8_t *decode_vbyte(const std::uint8_t *in, std::uint32_t &value) {
  std::uint32_t result = 0;
  std::uint32_t shift = 0;
  while (true) {
    const std::uint8_t byte = *in++;
    result |= static_cast<std::uint32_t>(byte & 127) << shift;
    if ((byte & 128) == 0) {
      break;
    }
    shift += 7;
  }
  value = result;
  return in;
}

} // namespace kestral
