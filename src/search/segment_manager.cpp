#include "kestral/search/segment_manager.hpp"

namespace kestral {

SegmentManager::SegmentManager(PublishedLexicalIndex &target_index,
                               std::size_t document_publish_threshold,
                               std::chrono::milliseconds time_publish_threshold)
    : target_index_(target_index),
      document_publish_threshold_(document_publish_threshold),
      time_publish_threshold_(time_publish_threshold),
      builder_(std::make_unique<LexicalSegmentBuilder>()),
      last_publish_time_(std::chrono::steady_clock::now()) {}

void SegmentManager::consume(std::span<const Document> documents) {
  std::unique_lock lock(mutex_);
  builder_->consume(documents);

  const auto now = std::chrono::steady_clock::now();
  const bool exceeded_docs =
      builder_->pending_document_count() >= document_publish_threshold_;
  const bool exceeded_time = (now - last_publish_time_) >= time_publish_threshold_;

  if ((exceeded_docs || exceeded_time) && builder_->pending_document_count() > 0) {
    // Publish!
    target_index_.publish_segment(std::move(*builder_).build());
    builder_ = std::make_unique<LexicalSegmentBuilder>();
    last_publish_time_ = std::chrono::steady_clock::now();
  }
}

void SegmentManager::force_publish() {
  std::unique_lock lock(mutex_);
  if (builder_->pending_document_count() > 0) {
    target_index_.publish_segment(std::move(*builder_).build());
    builder_ = std::make_unique<LexicalSegmentBuilder>();
    last_publish_time_ = std::chrono::steady_clock::now();
  }
}

} // namespace kestral
