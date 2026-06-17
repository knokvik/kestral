#pragma once

#include "kestral/core/document.hpp"
#include "kestral/core/document_batch_consumer.hpp"
#include "kestral/search/lexical_index.hpp"
#include <chrono>
#include <memory>
#include <mutex>
#include <span>

namespace kestral {

class SegmentManager : public DocumentBatchConsumer {
public:
  SegmentManager(PublishedLexicalIndex &target_index,
                 std::size_t document_publish_threshold = 10000,
                 std::chrono::milliseconds time_publish_threshold =
                     std::chrono::seconds(5));

  void consume(std::span<const Document> documents) override;
  void force_publish();

private:
  PublishedLexicalIndex &target_index_;
  std::size_t document_publish_threshold_;
  std::chrono::milliseconds time_publish_threshold_;

  std::mutex mutex_; // protects builder_ and last_publish_time_
  std::unique_ptr<LexicalSegmentBuilder> builder_;
  std::chrono::steady_clock::time_point last_publish_time_;
};

} // namespace kestral
