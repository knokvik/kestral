#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kestral {

class Tokenizer {
public:
  [[nodiscard]] std::vector<std::string> tokenize(std::string_view text) const;
  void tokenize_into(std::string_view text,
                     std::vector<std::string> &tokens) const;

private:
  [[nodiscard]] static bool is_term_character(unsigned char character);
};

} // namespace kestral
