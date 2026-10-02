#include "inferscale/api/fake_server.hpp"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstdlib>
#include <exception>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace inferscale::api {
namespace {

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;
using Json = nlohmann::json;
constexpr std::size_t kMaxRequestBytes = 64 * 1024;
constexpr std::size_t kOutputBufferBytes = 4096;
constexpr std::size_t kMaxConnections = 128;
std::atomic<std::uint64_t> g_next_request_id{1};

std::optional<std::uint64_t> next_request_id() {
  auto current = g_next_request_id.load(std::memory_order_relaxed);
  for (;;) {
    if (current == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    if (g_next_request_id.compare_exchange_weak(current, current + 1,
                                                std::memory_order_relaxed)) return current;
  }
}

std::uint32_t prompt_units(std::string_view prompt) {
  std::uint32_t count = 0;
  bool in_word = false;
  for (const unsigned char c : prompt) {
    if (std::isspace(c) != 0) {
      in_word = false;
    } else if (!in_word) {
      if (count == std::numeric_limits<std::uint32_t>::max()) return count;
      ++count;
      in_word = true;
    }
  }
  return std::max(count, 1U);
}

asio::awaitable<bool> write_bytes(beast::tcp_stream& stream, const std::string& bytes) {
  boost::system::error_code error;
  stream.expires_after(std::chrono::seconds(5));
  (void)co_await asio::async_write(stream, asio::buffer(bytes),
                                   asio::redirect_error(asio::use_awaitable, error));
  co_return !error;
}

asio::awaitable<bool> write_chunk(beast::tcp_stream& stream, std::string payload) {
  std::ostringstream size;
  size << std::hex << payload.size() << "\r\n";
  const auto wire = size.str() + payload + "\r\n";
  co_return co_await write_bytes(stream, wire);
}

asio::awaitable<void> write_error(beast::tcp_stream& stream, unsigned version,
                                  http::status status, std::string message) {
  http::response<http::string_body> response{status, version};
  response.set(http::field::server, "InferScale-FakeEngine");
  response.set(http::field::content_type, "application/json");
  response.keep_alive(false);
  response.body() = Json{{"error", {{"message", std::move(message)}}}}.dump();
  response.prepare_payload();
  stream.expires_after(std::chrono::seconds(5));
  boost::system::error_code ignored;
  (void)co_await http::async_write(stream, response,
                                   asio::redirect_error(asio::use_awaitable, ignored));
}

bool parse_deadline(const http::request<http::string_body>& request,
                    std::optional<ClockTime>& deadline) {
  const auto value = request.find("x-inferscale-deadline-ms");
  if (value == request.end()) return true;
  const std::string_view text(value->value().data(), value->value().size());
  std::uint32_t milliseconds = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), milliseconds);
  if (error != std::errc{} || end != text.data() + text.size() || milliseconds == 0 ||
      milliseconds > 3'600'000) return false;
  deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds);
  return true;
}

bool parse_request(const http::request<http::string_body>& request, Json& payload,
                   std::string& prompt, std::uint32_t& max_tokens) {
  if (request.method() != http::verb::post || request.target() != "/v1/completions") return false;
  const auto content_type = request.find(http::field::content_type);
  if (content_type == request.end() ||
      std::string_view(content_type->value().data(), content_type->value().size()).find("application/json") != 0) {
    return false;
  }
  payload = Json::parse(request.body(), nullptr, false);
  if (payload.is_discarded() || !payload.is_object()) return false;
  for (auto it = payload.begin(); it != payload.end(); ++it) {
    if (it.key() != "model" && it.key() != "prompt" && it.key() != "max_tokens" &&
        it.key() != "stream" && it.key() != "stream_options") return false;
  }
  if (!payload.contains("prompt") || !payload["prompt"].is_string() ||
      !payload.contains("max_tokens") || !payload["stream"].is_boolean() ||
      !payload["stream"].get<bool>()) return false;
  if (payload.contains("model") &&
      (!payload["model"].is_string() ||
       (payload["model"] != "fake" && payload["model"] != "local-model"))) return false;
  if (payload.contains("stream_options")) {
    const auto& options = payload["stream_options"];
    if (!options.is_object() || options.size() != 1 || !options.contains("include_usage") ||
        !options["include_usage"].is_boolean() || !options["include_usage"].get<bool>()) return false;
  }
  if (payload["max_tokens"].is_number_unsigned()) {
    const auto value = payload["max_tokens"].get<std::uint64_t>();
    if (value == 0 || value > 128) return false;
    max_tokens = static_cast<std::uint32_t>(value);
  } else if (payload["max_tokens"].is_number_integer()) {
    const auto value = payload["max_tokens"].get<std::int64_t>();
    if (value <= 0 || value > 128) return false;
    max_tokens = static_cast<std::uint32_t>(value);
  } else {
    return false;
  }
  prompt = payload["prompt"].get<std::string>();
  return !prompt.empty() && prompt.size() <= 32 * 1024;
}

asio::awaitable<void> serve_connection(tcp::socket socket, FakeRuntimeService& runtime,
                                       std::shared_ptr<std::atomic<std::size_t>> active) {
  struct ActiveGuard {
    std::shared_ptr<std::atomic<std::size_t>> count;
    ~ActiveGuard() { count->fetch_sub(1, std::memory_order_relaxed); }
  } guard{std::move(active)};

  beast::tcp_stream stream(std::move(socket));
  beast::flat_buffer buffer;
  http::request_parser<http::string_body> parser;
  parser.body_limit(kMaxRequestBytes);
  stream.expires_after(std::chrono::seconds(30));
  boost::system::error_code error;
  (void)co_await http::async_read(stream, buffer, parser,
                                  asio::redirect_error(asio::use_awaitable, error));
  if (error) {
    if (error == http::error::body_limit) {
      co_await write_error(stream, 11, http::status::payload_too_large, "request body exceeds 64 KiB");
    }
    co_return;
  }
  auto request = parser.release();
  const auto target = std::string_view(request.target().data(), request.target().size());
  if (request.method() == http::verb::get && (target == "/health" || target == "/healthz")) {
    const bool healthy = runtime.healthy();
    http::response<http::string_body> response{
        healthy ? http::status::ok : http::status::service_unavailable, request.version()};
    response.set(http::field::server, "InferScale-FakeEngine");
    response.set(http::field::content_type, "application/json");
    response.keep_alive(false);
    response.body() = healthy ? R"({"status":"ok","backend":"fake"})"
                              : R"({"status":"unavailable","backend":"fake"})";
    response.prepare_payload();
    stream.expires_after(std::chrono::seconds(5));
    boost::system::error_code ignored;
    (void)co_await http::async_write(stream, response,
                                     asio::redirect_error(asio::use_awaitable, ignored));
    co_return;
  }

  Json payload;
  std::string prompt;
  std::uint32_t max_tokens = 0;
  if (!parse_request(request, payload, prompt, max_tokens)) {
    co_await write_error(stream, request.version(), http::status::bad_request,
                         "expected strict streaming JSON for the fake backend");
    co_return;
  }
  std::optional<ClockTime> deadline;
  if (!parse_deadline(request, deadline)) {
    co_await write_error(stream, request.version(), http::status::bad_request,
                         "invalid x-inferscale-deadline-ms");
    co_return;
  }
  const auto id = next_request_id();
  if (!id) {
    co_await write_error(stream, request.version(), http::status::service_unavailable,
                         "request ID space exhausted");
    co_return;
  }
  auto output = std::make_shared<StreamBuffer>(kOutputBufferBytes);
  auto admission = std::make_shared<std::atomic<AdmissionResult>>(AdmissionResult::Pending);
  const RequestKey key{*id, 1};
  NewRequestEvent event{RequestSpec{*id, prompt_units(prompt), max_tokens, deadline,
                                    static_cast<std::uint32_t>(kOutputBufferBytes)},
                        1, output, admission};
  if (!runtime.submit(std::move(event))) {
    admission->store(AdmissionResult::Rejected, std::memory_order_release);
    co_await write_error(stream, request.version(), http::status::service_unavailable,
                         "runtime event queue is full");
    co_return;
  }

  asio::steady_timer timer(co_await asio::this_coro::executor);
  for (int attempt = 0; attempt < 1000 &&
       admission->load(std::memory_order_acquire) == AdmissionResult::Pending; ++attempt) {
    timer.expires_after(std::chrono::milliseconds(1));
    co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, error));
  }
  if (admission->load(std::memory_order_acquire) != AdmissionResult::Accepted) {
    runtime.cancel(key);
    co_await write_error(stream, request.version(), http::status::too_many_requests,
                         "runtime admission rejected or timed out");
    co_return;
  }

  http::response<http::empty_body> response{http::status::ok, request.version()};
  response.set(http::field::server, "InferScale-FakeEngine");
  response.set(http::field::content_type, "text/event-stream");
  response.set(http::field::cache_control, "no-cache");
  response.keep_alive(false);
  response.chunked(true);
  http::response_serializer<http::empty_body> serializer(response);
  stream.expires_after(std::chrono::seconds(5));
  (void)co_await http::async_write_header(stream, serializer,
                                          asio::redirect_error(asio::use_awaitable, error));
  if (error) {
    runtime.cancel(key);
    co_return;
  }

  for (;;) {
    auto chunk = output->consume_with_status(512);
    if (chunk.data.empty() && chunk.closed) break;
    auto& data = chunk.data;
    if (!data.empty()) {
      (void)runtime.post(StreamWritableEvent{key});
      const Json event_json{{"choices", Json::array({Json{{"text", std::move(data)}}})}};
      if (!co_await write_chunk(stream, "data: " + event_json.dump() + "\n\n")) {
        runtime.cancel(key);
        co_return;
      }
      continue;
    }
    timer.expires_after(std::chrono::milliseconds(2));
    co_await timer.async_wait(asio::redirect_error(asio::use_awaitable, error));
  }

  if (output->end_reason() == StreamEnd::Complete) {
    const auto stream_usage = output->usage();
    const Json usage_event{{"choices", Json::array()},
                           {"usage", {{"prompt_tokens", stream_usage.prompt_tokens},
                                       {"completion_tokens", stream_usage.output_tokens}}}};
    if (!co_await write_chunk(stream, "data: " + usage_event.dump() + "\n\n")) {
      co_return;
    }
    if (!co_await write_chunk(stream, "data: [DONE]\n\n")) {
      co_return;
    }
  } else {
    const auto message = output->end_reason() == StreamEnd::Cancelled ? "request cancelled" : "request failed";
    const Json failure{{"error", {{"message", message}}}};
    if (!co_await write_chunk(stream, "data: " + failure.dump() + "\n\n")) co_return;
  }
  (void)co_await write_bytes(stream, "0\r\n\r\n");
}

}  // namespace

FakeRuntimeService::FakeRuntimeService()
    : reactor_(clock_, 64, 1024, 32768), engine_({64, 128, 32768, false}) {
  executor_thread_ = std::thread([this] { executor_loop(); });
  runtime_thread_ = std::thread([this] { runtime_loop(); });
}

FakeRuntimeService::~FakeRuntimeService() {
  request_shutdown();
  if (!wait_for_shutdown(std::chrono::seconds(10))) std::_Exit(124);
  if (runtime_thread_.joinable()) runtime_thread_.join();
  if (executor_thread_.joinable()) executor_thread_.join();
}

bool FakeRuntimeService::submit(NewRequestEvent event) {
  const auto admission = event.admission;
  if (shutdown_requested_.load(std::memory_order_acquire) ||
      runtime_failed_.load(std::memory_order_acquire) ||
      runtime_stopped_.load(std::memory_order_acquire)) {
    if (admission) admission->store(AdmissionResult::Rejected, std::memory_order_release);
    return false;
  }
  const bool accepted = reactor_.post(std::move(event));
  if (!accepted && admission) admission->store(AdmissionResult::Rejected, std::memory_order_release);
  return accepted;
}

bool FakeRuntimeService::post(RuntimeEvent event) { return reactor_.post(std::move(event)); }

void FakeRuntimeService::cancel(RequestKey key) { (void)post(CancelEvent{key}); }

void FakeRuntimeService::request_shutdown() {
  if (runtime_stopped_.load(std::memory_order_acquire)) return;
  if (!shutdown_requested_.exchange(true, std::memory_order_acq_rel)) {
    (void)reactor_.post(ShutdownEvent{});
  }
}

bool FakeRuntimeService::wait_for_shutdown(std::chrono::milliseconds timeout) {
  std::unique_lock lock(state_mutex_);
  return state_changed_.wait_for(lock, timeout, [this] {
    return runtime_stopped_.load(std::memory_order_acquire) &&
           executor_stopped_.load(std::memory_order_acquire);
  });
}

void FakeRuntimeService::notify_state_change() { state_changed_.notify_all(); }

void FakeRuntimeService::runtime_loop() {
  bool batch_active = false;
  for (;;) {
    reactor_.run_ready();
    reactor_.expire_deadlines();
    if (reactor_.fatal_error()) {
      runtime_failed_.store(true, std::memory_order_release);
      reactor_.fail_all();
    }
    if (batch_active && reactor_.inflight_ticket_count() == 0) batch_active = false;

    for (const auto& request : reactor_.take_clear_requests()) {
      outstanding_engine_commands_.fetch_add(1, std::memory_order_relaxed);
      if (!engine_commands_.push_wait(ClearCommand{request})) std::terminate();
    }

    if (!batch_active && !reactor_.shutting_down() && !reactor_.fatal_error()) {
      const auto plan = reactor_.make_fifo_plan(128, 16, 64);
      if (!plan.items.empty()) {
        auto ticket = reactor_.issue(plan);
        if (ticket) {
          outstanding_engine_commands_.fetch_add(1, std::memory_order_relaxed);
          if (!engine_commands_.push_wait(ExecuteCommand{std::move(*ticket)})) std::terminate();
          batch_active = true;
        }
      }
    }

    if ((reactor_.shutdown_complete() || reactor_.fatal_error()) && !batch_active &&
        outstanding_engine_commands_.load(std::memory_order_acquire) == 0) {
      reactor_.seal_events();
      (void)engine_commands_.push_wait(StopCommand{});
      {
        std::lock_guard lock(state_mutex_);
        runtime_stopped_.store(true, std::memory_order_release);
      }
      notify_state_change();
      return;
    }
    (void)reactor_.wait_one_for(std::chrono::milliseconds(2));
  }
}

void FakeRuntimeService::executor_loop() {
  while (auto command = engine_commands_.wait_pop()) {
    std::visit([this](auto&& value) {
      using Command = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<Command, ExecuteCommand>) {
        EngineResult result;
        try {
          result = engine_.execute(value.ticket);
        } catch (const std::exception& error) {
          result = {false, FailureClass::ProcessFatal, {}, false, error.what()};
        } catch (...) {
          result = {false, FailureClass::ProcessFatal, {}, false, "unknown engine exception"};
        }
        if (!reactor_.post_completion(BatchCompletedEvent{std::move(value.ticket), std::move(result)})) {
          std::terminate();
        }
        outstanding_engine_commands_.fetch_sub(1, std::memory_order_release);
      } else if constexpr (std::is_same_v<Command, ClearCommand>) {
        bool cleared = false;
        try {
          cleared = engine_.clear_sequence(value.request.sequence);
        } catch (...) {
          cleared = false;
        }
        if (!reactor_.post_completion(SequenceClearedEvent{value.request.key, cleared})) {
          std::terminate();
        }
        outstanding_engine_commands_.fetch_sub(1, std::memory_order_release);
      } else {
        engine_commands_.close();
        {
          std::lock_guard lock(state_mutex_);
          executor_stopped_.store(true, std::memory_order_release);
        }
        notify_state_change();
      }
    }, std::move(*command));
    if (executor_stopped_.load(std::memory_order_acquire)) return;
  }
}

SseServer::SseServer(asio::io_context& context, const tcp::endpoint& endpoint,
                     FakeRuntimeService& runtime)
    : context_(context), acceptor_(context), runtime_(runtime),
      active_connections_(std::make_shared<std::atomic<std::size_t>>(0)) {
  boost::system::error_code error;
  acceptor_.open(endpoint.protocol(), error);
  if (error) throw boost::system::system_error(error);
  acceptor_.set_option(asio::socket_base::reuse_address(true), error);
  if (error) throw boost::system::system_error(error);
  acceptor_.bind(endpoint, error);
  if (error) throw boost::system::system_error(error);
  acceptor_.listen(asio::socket_base::max_listen_connections, error);
  if (error) throw boost::system::system_error(error);
}

SseServer::~SseServer() {
  boost::system::error_code ignored;
  acceptor_.close(ignored);
}

void SseServer::start() {
  if (started_ || stopped_.load(std::memory_order_acquire)) {
    throw std::logic_error("SseServer can be started exactly once before stop");
  }
  started_ = true;
  accept_next();
}

void SseServer::stop() {
  if (stopped_.exchange(true, std::memory_order_acq_rel)) return;
  const auto weak = weak_from_this();
  asio::post(context_, [weak] {
    const auto self = weak.lock();
    if (!self) return;
    boost::system::error_code ignored;
    self->acceptor_.cancel(ignored);
    self->acceptor_.close(ignored);
  });
}

std::uint16_t SseServer::port() const {
  boost::system::error_code error;
  const auto endpoint = acceptor_.local_endpoint(error);
  if (error) throw boost::system::system_error(error);
  return endpoint.port();
}

void SseServer::accept_next() {
  const auto weak = weak_from_this();
  acceptor_.async_accept([weak](boost::system::error_code error, tcp::socket socket) {
    const auto self = weak.lock();
    if (!self) return;
    if (!error && !self->stopped_.load(std::memory_order_acquire)) {
      const auto active = self->active_connections_->fetch_add(1, std::memory_order_relaxed) + 1;
      if (active > kMaxConnections) {
        self->active_connections_->fetch_sub(1, std::memory_order_relaxed);
        boost::system::error_code ignored;
        socket.close(ignored);
      } else {
        asio::co_spawn(self->context_,
                       serve_connection(std::move(socket), self->runtime_, self->active_connections_),
                       asio::detached);
      }
    }
    if (!self->stopped_.load(std::memory_order_acquire)) self->accept_next();
  });
}

}  // namespace inferscale::api
