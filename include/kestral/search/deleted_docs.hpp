#pragma once

#include <cstdint>
#include <shared_mutex>
#include <unordered_set>

namespace kestral {

class DeletedDocs {
public:
  void mark_deleted(std::uint64_t document_id) {
    std::unique_lock lock(mutex_);
    deleted_ids_.insert(document_id);
  }

  [[nodiscard]] bool is_deleted(std::uint64_t document_id) const {
    std::shared_lock lock(mutex_);
    return deleted_ids_.find(document_id) != deleted_ids_.end();
  }

private:
  mutable std::shared_mutex mutex_;
  std::unordered_set<std::uint64_t> deleted_ids_;
};

} // namespace kestral
