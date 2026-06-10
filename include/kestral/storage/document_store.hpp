#pragma once

#include "kestral/core/document.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#include <rocksdb/db.h>

namespace kestral {

struct DocumentStoreOptions {
  std::string db_path = "./document_db";
  bool create_if_missing = true;
  bool disable_wal = true;
  int max_background_jobs = 4;
  std::size_t write_buffer_size_mb = 64;
};

class DocumentStore {
public:
  explicit DocumentStore(DocumentStoreOptions options);
  ~DocumentStore();

  void write_batch(std::span<const Document> documents);
  [[nodiscard]] std::optional<Document> read_document(std::uint64_t id) const;
  void flush();

private:
  [[nodiscard]] rocksdb::WriteOptions build_write_options() const;
  [[nodiscard]] std::string serialize_document(const Document &document) const;
  [[nodiscard]] Document deserialize_document(std::uint64_t id,
                                              std::string_view payload) const;

  DocumentStoreOptions options_;
  std::unique_ptr<rocksdb::DB> db_;
};

} // namespace kestral
