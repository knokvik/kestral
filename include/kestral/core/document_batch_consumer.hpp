#pragma once

#include "kestral/core/document.hpp"

#include <span>

namespace kestral {

class DocumentBatchConsumer {
public:
  virtual ~DocumentBatchConsumer() = default;

  virtual void consume(std::span<const Document> documents) = 0;
};

} // namespace kestral
