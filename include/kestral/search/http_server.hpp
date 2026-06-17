#pragma once

#include "kestral/search/hybrid_search.hpp"
#include <cstdint>
#include <memory>
#include <string>

namespace kestral {

class HttpServer {
public:
  HttpServer(const HybridSearchEngine &search_engine, const std::string &host = "0.0.0.0", int port = 8080);
  ~HttpServer();

  void start();
  void stop();

private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace kestral
