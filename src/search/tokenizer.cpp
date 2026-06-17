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

void Tokenizer::tokenize_views(std::string_view text, std::string &scratch,
                                std::vector<std::string_view> &tokens) const {
  tokens.clear();
  scratch.clear();
  // Reserve worst-case (every char is a term char). This guarantees no
  // reallocation, keeping all returned string_views valid.
  scratch.reserve(text.size());

  std::size_t token_start = 0;
  bool in_token = false;

  for (const unsigned char ch : text) {
    if (is_term_character(ch)) {
      if (!in_token) {
        token_start = scratch.size();
        in_token = true;
      }
      scratch.push_back(
          static_cast<char>(std::tolower(static_cast<int>(ch))));
    } else if (in_token) {
      tokens.emplace_back(scratch.data() + token_start,
                          scratch.size() - token_start);
      in_token = false;
    }
  }

  if (in_token) {
    tokens.emplace_back(scratch.data() + token_start,
                        scratch.size() - token_start);
  }
}

bool Tokenizer::is_term_character(unsigned char character) {
  return std::isalnum(character) != 0;
}

} // namespace kestral
