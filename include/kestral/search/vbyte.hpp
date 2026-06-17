#pragma once

#include <cstdint>
#include <vector>

namespace kestral {

void encode_vbyte(std::uint32_t value, std::vector<std::uint8_t> &out);
const std::uint8_t *decode_vbyte(const std::uint8_t *in, std::uint32_t &value);

} // namespace kestral
