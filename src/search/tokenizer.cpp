#include "kestral/search/tokenizer.hpp"

#include <cctype>

namespace kestral {

std::vector<std::string> Tokenizer::tokenize(std::string_view text) const {
  std::vector<std::string> tokens;
  tokenize_into(text, tokens);
  return tokens;
}

void Tokenizer::tokenize_into(std::string_view text,
                              std::vector<std::string> &tokens) const {
  tokens.clear();

  std::string current_token;
  current_token.reserve(24);

  for (const unsigned char character : text) {
    if (is_term_character(character)) {
      current_token.push_back(
          static_cast<char>(std::tolower(static_cast<int>(character))));
      continue;
    }

    if (!current_token.empty()) {
      tokens.push_back(current_token);
      current_token.clear();
    }
  }

  if (!current_token.empty()) {
    tokens.push_back(current_token);
  }
}

bool Tokenizer::is_term_character(unsigned char character) {
  return std::isalnum(character) != 0;
}

} // namespace kestral
