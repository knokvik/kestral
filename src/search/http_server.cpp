#include "kestral/search/http_server.hpp"

#include <httplib.h>
#include <nlohmann/json.hpp>
#include <spdlog/spdlog.h>
#include <thread>

namespace kestral {

using json = nlohmann::json;

struct HttpServer::Impl {
  const HybridSearchEngine &search_engine;
  httplib::Server server;
  std::string host;
  int port;
  std::thread server_thread;

  Impl(const HybridSearchEngine &se, const std::string &h, int p)
      : search_engine(se), host(h), port(p) {

    server.Post("/search", [this](const httplib::Request &req, httplib::Response &res) {
      try {
        auto req_json = json::parse(req.body);

        std::string query_text = req_json.value("query", "");
        std::vector<float> query_vector;
        if (req_json.contains("vector") && req_json["vector"].is_array()) {
          query_vector = req_json["vector"].get<std::vector<float>>();
        }

        std::size_t top_k = req_json.value("top_k", 10);
        float rrf_k = req_json.value("rrf_k", 60.0f);

        auto results = search_engine.search(query_text, query_vector, top_k, rrf_k);

        json res_json = json::array();
        for (const auto &r : results) {
          res_json.push_back({
            {"document_id", r.document_id},
            {"score", r.score}
          });
        }

        res.set_content(res_json.dump(), "application/json");

      } catch (const std::exception &e) {
        res.status = 400;
        json error = {{"error", e.what()}};
        res.set_content(error.dump(), "application/json");
      }
    });
  }
};

HttpServer::HttpServer(const HybridSearchEngine &search_engine, const std::string &host, int port)
    : impl_(std::make_unique<Impl>(search_engine, host, port)) {}

HttpServer::~HttpServer() {
  stop();
}

void HttpServer::start() {
  spdlog::info("Starting HTTP Server on {}:{}", impl_->host, impl_->port);
  impl_->server_thread = std::thread([this]() {
    impl_->server.listen(impl_->host.c_str(), impl_->port);
  });
}

void HttpServer::stop() {
  if (impl_->server.is_running()) {
    impl_->server.stop();
  }
  if (impl_->server_thread.joinable()) {
    impl_->server_thread.join();
  }
}

} // namespace kestral
