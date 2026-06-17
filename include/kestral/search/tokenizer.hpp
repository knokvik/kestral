#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace kestral {

class Tokenizer {
public:
  /// Returns a vector of lowercased token strings (allocating).
  [[nodiscard]] std::vector<std::string> tokenize(std::string_view text) const;

  /// Appends lowercased token strings into `tokens` (allocating, clears first).
  void tokenize_into(std::string_view text,
                     std::vector<std::string> &tokens) const;

  /// Zero-copy tokenization. Lowercased characters are written into `scratch`,
  /// and `tokens` receives string_views pointing into `scratch`.
  /// `scratch` is reserved to `text.size()` to prevent reallocation, so all
  /// returned views remain valid as long as `scratch` is not modified.
  void tokenize_views(std::string_view text, std::string &scratch,
                      std::vector<std::string_view> &tokens) const;

private:
  [[nodiscard]] static bool is_term_character(unsigned char character);
};

} // namespace kestral
