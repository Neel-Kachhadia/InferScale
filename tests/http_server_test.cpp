#include "inferscale/api/fake_server.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <chrono>
#include <atomic>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

namespace {

http::response<http::string_body> post(std::uint16_t port, std::string body,
                                      std::string priority = {}) {
  asio::io_context context;
  beast::tcp_stream socket(context);
  socket.connect({asio::ip::address_v4::loopback(), port});
  socket.expires_after(std::chrono::seconds(5));
  http::request<http::string_body> request{http::verb::post, "/v1/completions", 11};
  request.set(http::field::host, "127.0.0.1");
  request.set(http::field::content_type, "application/json");
  if (!priority.empty()) request.set("x-inferscale-priority", priority);
  request.body() = std::move(body);
  request.prepare_payload();
  http::write(socket, request);
  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(socket, buffer, response);
  return response;
}

http::response<http::string_body> get(std::uint16_t port, std::string target) {
  asio::io_context context;
  beast::tcp_stream socket(context);
  socket.connect({asio::ip::address_v4::loopback(), port});
  socket.expires_after(std::chrono::seconds(5));
  http::request<http::empty_body> request{http::verb::get, std::move(target), 11};
  request.set(http::field::host, "127.0.0.1");
  http::write(socket, request);
  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(socket, buffer, response);
  return response;
}

}  // namespace

int main() {
  asio::io_context context(1);
  inferscale::api::FakeRuntimeService runtime;
  std::shared_ptr<inferscale::api::SseServer> server;
  std::thread io_thread;
  try {
    server = std::make_shared<inferscale::api::SseServer>(
        context, tcp::endpoint(asio::ip::address_v4::loopback(), 0), runtime);
    server->start();
    io_thread = std::thread([&context] { context.run(); });

    auto response = post(server->port(),
        R"({"model":"fake","prompt":"one two three","max_tokens":2,"stream":true,"stream_options":{"include_usage":true}})");
    if (response.result() != http::status::ok ||
        response[http::field::content_type] != "text/event-stream" ||
        response.body().find("data: ") == std::string::npos ||
        response.body().find("\"completion_tokens\":2") == std::string::npos ||
        response.body().find("data: [DONE]") == std::string::npos) {
      throw std::runtime_error("streaming completion response did not contain expected SSE and usage");
    }
    if (get(server->port(), "/healthz").result() != http::status::ok) {
      throw std::runtime_error("healthy fake runtime did not report healthy status");
    }
    const auto ready = get(server->port(), "/ready");
    if (ready.result() != http::status::ok ||
        ready.body().find("\"ready\":true") == std::string::npos) {
      throw std::runtime_error("capacity-available fake runtime did not report ready");
    }
    const auto metrics = get(server->port(), "/metrics");
    if (metrics.result() != http::status::ok ||
        metrics.body().find("\"adaptive_prefill\"") == std::string::npos ||
        metrics.body().find("\"scheduler_wait_p99_ms\"") == std::string::npos ||
        metrics.body().find("\"scheduler_wait_histogram\"") == std::string::npos ||
        metrics.body().find("\"hard_usable_tokens\"") == std::string::npos ||
        metrics.body().find("\"units\":\"logical_tokens\"") == std::string::npos ||
        metrics.body().find("\"hard_limit_aborted\"") == std::string::npos) {
      throw std::runtime_error("bounded scheduling and resource metrics were not exposed");
    }
    const auto batch_response = post(server->port(),
        R"({"model":"fake","prompt":"batch request","max_tokens":1,"stream":true})",
        "batch");
    if (batch_response.result() != http::status::ok) {
      throw std::runtime_error("explicit batch traffic class was not accepted");
    }
    if (post(server->port(),
        R"({"model":"fake","prompt":"bad priority","max_tokens":1,"stream":true})",
        "urgent").result() != http::status::bad_request) {
      throw std::runtime_error("invalid traffic class header was not rejected");
    }

    auto invalid = post(server->port(),
        R"({"model":"fake","prompt":"x","max_tokens":1,"stream":true,"extra":1})");
    if (invalid.result() != http::status::bad_request) {
      throw std::runtime_error("unsupported JSON field was not rejected");
    }
    for (const auto& body : {"{", "[]", "null", "\"text\"",
                             R"({"model":"/tmp/model.gguf","prompt":"x","max_tokens":1,"stream":true})",
                             R"({"model":"fake","prompt":"x","max_tokens":129,"stream":true})",
                             R"({"model":"fake","prompt":"x","max_tokens":1,"stream":false})"}) {
      if (post(server->port(), body).result() != http::status::bad_request) {
        throw std::runtime_error("malformed or unsupported request corpus member was not rejected");
      }
    }
    const std::string strict_seed =
        R"({"model":"fake","prompt":"fuzz","max_tokens":1,"stream":true})";
    std::uint32_t fuzz_state = 0x5eed1234U;
    for (std::uint32_t case_index = 0; case_index < 2048; ++case_index) {
      fuzz_state = fuzz_state * 1664525U + 1013904223U;
      const auto suffix_size = 1U + (fuzz_state % 8U);
      std::string mutated = strict_seed;
      for (std::uint32_t byte_index = 0; byte_index < suffix_size; ++byte_index) {
        fuzz_state = fuzz_state * 1664525U + 1013904223U;
        mutated.push_back(static_cast<char>(0x21U + (fuzz_state % 0xdfU)));
      }
      if (post(server->port(), std::move(mutated)).result() != http::status::bad_request) {
        throw std::runtime_error("deterministic strict-JSON fuzz smoke accepted a trailing-garbage case");
      }
    }
    if (post(server->port(), std::string(65 * 1024, 'x')).result() != http::status::payload_too_large) {
      throw std::runtime_error("oversized request body was not rejected");
    }

    runtime.pause_engine_before_execute();
    auto first_stream = std::make_shared<inferscale::StreamBuffer>(64);
    auto waiting_stream = std::make_shared<inferscale::StreamBuffer>(64);
    auto first_admission = std::make_shared<std::atomic<inferscale::AdmissionResult>>(
        inferscale::AdmissionResult::Pending);
    auto waiting_admission = std::make_shared<std::atomic<inferscale::AdmissionResult>>(
        inferscale::AdmissionResult::Pending);
    if (!runtime.submit({inferscale::RequestSpec{2001, 1, 1, std::nullopt, 64}, 0,
                         first_stream, first_admission})) {
      throw std::runtime_error("fake runtime rejected fatal-path test admissions");
    }
    const auto admission_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (first_admission->load(std::memory_order_acquire) == inferscale::AdmissionResult::Pending &&
           std::chrono::steady_clock::now() < admission_deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (first_admission->load(std::memory_order_acquire) != inferscale::AdmissionResult::Accepted ||
        !runtime.wait_until_engine_paused(std::chrono::seconds(2))) {
      throw std::runtime_error("first fatal-path request did not reach the engine barrier");
    }
    if (!runtime.submit({inferscale::RequestSpec{2002, 1, 1, std::nullopt, 64}, 0,
                         waiting_stream, waiting_admission})) {
      throw std::runtime_error("fake runtime rejected the waiting fatal-path request");
    }
    const auto waiting_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (waiting_admission->load(std::memory_order_acquire) == inferscale::AdmissionResult::Pending &&
           std::chrono::steady_clock::now() < waiting_deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (waiting_admission->load(std::memory_order_acquire) != inferscale::AdmissionResult::Accepted) {
      throw std::runtime_error("waiting fatal-path request was not admitted");
    }
    runtime.fail_next_engine(inferscale::FailureClass::ProcessFatal);
    runtime.resume_engine();
    const auto failure_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while ((!first_stream->closed() || !waiting_stream->closed()) &&
           std::chrono::steady_clock::now() < failure_deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if (!first_stream->closed() || !waiting_stream->closed() || runtime.healthy()) {
      throw std::runtime_error("fatal engine failure left an admitted stream open or healthy");
    }
    if (get(server->port(), "/healthz").result() != http::status::service_unavailable) {
      throw std::runtime_error("fatal fake runtime still reported healthy");
    }
    if (get(server->port(), "/ready").result() != http::status::service_unavailable) {
      throw std::runtime_error("fatal fake runtime still reported ready for admission");
    }
    if (!runtime.wait_for_shutdown(std::chrono::seconds(3))) {
      throw std::runtime_error("fatal fake runtime did not retire within 3 seconds");
    }

    server->stop();
    runtime.request_shutdown();
    io_thread.join();
    if (!runtime.wait_for_shutdown(std::chrono::seconds(3))) {
      throw std::runtime_error("fake runtime did not shut down within 3 seconds");
    }
    std::cout << "HTTP/SSE integration passed: stream, strict parser corpus, FUZZ SMOKE 2048, "
                 "body bound, fatal drain, shutdown\n";
    return 0;
  } catch (const boost::system::system_error& error) {
    if (io_thread.joinable()) {
      if (server) server->stop();
      runtime.resume_engine();
      runtime.request_shutdown();
      io_thread.join();
    }
    const auto condition = error.code().default_error_condition();
    if (condition == boost::system::errc::make_error_condition(boost::system::errc::operation_not_permitted) ||
        condition == boost::system::errc::make_error_condition(boost::system::errc::address_not_available)) {
      std::cout << "SKIP: local loopback sockets unavailable: " << error.what() << '\n';
      return 77;
    }
    std::cerr << error.what() << '\n';
    return 1;
  } catch (const std::exception& error) {
    if (server) server->stop();
    runtime.resume_engine();
    runtime.request_shutdown();
    if (io_thread.joinable()) io_thread.join();
    (void)runtime.wait_for_shutdown(std::chrono::seconds(3));
    std::cerr << error.what() << '\n';
    return 1;
  }
}
