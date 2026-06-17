#pragma once

#include "kestral/search/lexical_index.hpp"
#include <chrono>
#include <list>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace kestral {

struct CacheKey {
  std::string query;
  std::size_t top_k;
  bool require_all_terms;

  bool operator==(const CacheKey &other) const {
    return query == other.query && top_k == other.top_k &&
           require_all_terms == other.require_all_terms;
  }
};

struct CacheKeyHash {
  std::size_t operator()(const CacheKey &key) const {
    std::size_t h1 = std::hash<std::string>{}(key.query);
    std::size_t h2 = std::hash<std::size_t>{}(key.top_k);
    std::size_t h3 = std::hash<bool>{}(key.require_all_terms);
    return h1 ^ (h2 << 1) ^ (h3 << 2);
  }
};

class QueryCache {
public:
  explicit QueryCache(std::size_t max_entries = 1000,
                      std::chrono::milliseconds ttl = std::chrono::minutes(5));

  std::optional<std::vector<SearchResult>> get(const CacheKey &key);
  void put(const CacheKey &key, const std::vector<SearchResult> &results);
  void clear();
  std::size_t size() const;

private:
  struct CacheEntry {
    std::vector<SearchResult> results;
    std::chrono::steady_clock::time_point timestamp;
  };

  std::size_t max_entries_;
  std::chrono::milliseconds ttl_;

  mutable std::shared_mutex mutex_;
  std::list<CacheKey> lru_list_;
  std::unordered_map<CacheKey, std::pair<CacheEntry, std::list<CacheKey>::iterator>, CacheKeyHash> map_;
};

} // namespace kestral
