#include "kestral/search/query_cache.hpp"

namespace kestral {

QueryCache::QueryCache(std::size_t max_entries, std::chrono::milliseconds ttl)
    : max_entries_(max_entries), ttl_(ttl) {}

std::optional<std::vector<SearchResult>> QueryCache::get(const CacheKey &key) {
  std::shared_lock lock(mutex_);
  
  auto it = map_.find(key);
  if (it == map_.end()) {
    return std::nullopt;
  }

  const auto now = std::chrono::steady_clock::now();
  if (now - it->second.first.timestamp > ttl_) {
    return std::nullopt; // Expired, we'll let a future put() or eviction clean it up
  }

  // Technically we should move it to the front of the LRU list, but that requires
  // a unique_lock which blocks concurrent reads. 
  // For a high-throughput search engine, we often accept that read-only access
  // doesn't update LRU strictly, OR we upgrade the lock. 
  // Let's upgrade to a write lock only if we decide to strictly enforce LRU on read.
  // We'll skip LRU update on read to maximize concurrent throughput.

  return it->second.first.results;
}

void QueryCache::put(const CacheKey &key, const std::vector<SearchResult> &results) {
  std::unique_lock lock(mutex_);

  auto it = map_.find(key);
  if (it != map_.end()) {
    // Update existing
    it->second.first.results = results;
    it->second.first.timestamp = std::chrono::steady_clock::now();
    
    // Move to front of LRU
    lru_list_.erase(it->second.second);
    lru_list_.push_front(key);
    it->second.second = lru_list_.begin();
    return;
  }

  // Insert new
  if (map_.size() >= max_entries_ && !lru_list_.empty()) {
    // Evict least recently used
    const auto lru_key = lru_list_.back();
    map_.erase(lru_key);
    lru_list_.pop_back();
  }

  lru_list_.push_front(key);
  map_[key] = {
      CacheEntry{results, std::chrono::steady_clock::now()},
      lru_list_.begin()
  };
}

void QueryCache::clear() {
  std::unique_lock lock(mutex_);
  map_.clear();
  lru_list_.clear();
}

std::size_t QueryCache::size() const {
  std::shared_lock lock(mutex_);
  return map_.size();
}

} // namespace kestral
