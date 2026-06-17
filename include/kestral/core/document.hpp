#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace kestral {

struct Document {
  std::uint64_t id = 0;
  std::uint64_t timestamp = 0;
  std::string title;
  std::string body;
  std::vector<float> embedding;
};

class DocumentBatch {
public:
  DocumentBatch() = default;

  explicit DocumentBatch(std::size_t reserve_count) { reserve(reserve_count); }

  void reserve(std::size_t reserve_count) { documents_.reserve(reserve_count); }

  void clear() { documents_.clear(); }

  void add(Document document) { documents_.push_back(std::move(document)); }

  [[nodiscard]] bool empty() const { return documents_.empty(); }

  [[nodiscard]] std::size_t size() const { return documents_.size(); }

  [[nodiscard]] std::span<Document> documents() { return documents_; }

  [[nodiscard]] std::span<const Document> documents() const { return documents_; }

private:
  std::vector<Document> documents_;
};

} // namespace kestral
