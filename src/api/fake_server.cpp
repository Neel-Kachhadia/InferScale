#include "inferscale/api/fake_server.hpp"
#include "inferscale/runtime/nvtx.hpp"

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
#include <functional>
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
constexpr std::size_t kMaxConnections = 128;
std::atomic<std::uint64_t> g_next_request_id{1};

EngineCapabilities require_capabilities(const std::unique_ptr<InferenceEngine>& engine) {
  if (!engine) throw std::invalid_argument("runtime service requires an inference engine");
  return engine->capabilities();
}

std::optional<std::uint64_t> next_request_id() {
  auto current = g_next_request_id.load(std::memory_order_relaxed);
  for (;;) {
    if (current == std::numeric_limits<std::uint64_t>::max()) return std::nullopt;
    if (g_next_request_id.compare_exchange_weak(current, current + 1,
                                                std::memory_order_relaxed)) return current;
  }
}

asio::awaitable<bool> write_bytes(beast::tcp_stream& stream, const std::string& bytes) {
  INFERSCALE_NVTX_RANGE("SSE/SocketWrite");
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
                                  http::status status, std::string message,
                                  const std::string& server_name) {
  http::response<http::string_body> response{status, version};
  response.set(http::field::server, server_name);
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

bool parse_request_class(const http::request<http::string_body>& request,
                         RequestClass& service_class) {
  const auto value = request.find("x-inferscale-priority");
  if (value == request.end()) {
    service_class = RequestClass::Interactive;
    return true;
  }
  const std::string_view text(value->value().data(), value->value().size());
  if (text == "interactive") {
    service_class = RequestClass::Interactive;
    return true;
  }
  if (text == "batch") {
    service_class = RequestClass::Batch;
    return true;
  }
  return false;
}

bool parse_slo_header(const http::request<http::string_body>& request,
                      std::string_view name,
                      std::optional<std::chrono::milliseconds>& slo) {
  const auto value = request.find(name.data());
  if (value == request.end()) return true;
  const std::string_view text(value->value().data(), value->value().size());
  std::uint32_t milliseconds = 0;
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), milliseconds);
  if (error != std::errc{} || end != text.data() + text.size() || milliseconds == 0 ||
      milliseconds > 3'600'000) return false;
  slo = std::chrono::milliseconds(milliseconds);
  return true;
}

bool parse_request(const http::request<http::string_body>& request, Json& payload,
                   std::string& prompt, std::uint32_t& max_tokens,
                   std::string_view model_alias) {
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
       payload["model"].get_ref<const std::string&>() != model_alias)) return false;
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

asio::awaitable<void> serve_connection(
    tcp::socket socket, RuntimeService& runtime,
    std::shared_ptr<std::atomic<std::size_t>> active,
    asio::strand<asio::io_context::executor_type> connection_strand,
    std::size_t output_buffer_bytes) {
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
      co_await write_error(stream, 11, http::status::payload_too_large,
                           "request body exceeds 64 KiB", runtime.server_name());
    }
    co_return;
  }
  auto request = parser.release();
  const auto target = std::string_view(request.target().data(), request.target().size());
  if (request.method() == http::verb::get && (target == "/health" || target == "/healthz")) {
    const bool healthy = runtime.healthy();
    http::response<http::string_body> response{
        healthy ? http::status::ok : http::status::service_unavailable, request.version()};
    response.set(http::field::server, runtime.server_name());
    response.set(http::field::content_type, "application/json");
    response.keep_alive(false);
    response.body() = Json{{"status", healthy ? "ok" : "unavailable"},
                           {"backend", runtime.backend_name()}}.dump();
    response.prepare_payload();
    stream.expires_after(std::chrono::seconds(5));
    boost::system::error_code ignored;
    (void)co_await http::async_write(stream, response,
                                     asio::redirect_error(asio::use_awaitable, ignored));
    co_return;
  }
  if (request.method() == http::verb::get && target == "/ready") {
    const bool ready = runtime.ready_for_admission();
    const auto metrics = runtime.metrics_snapshot();
    http::response<http::string_body> response{
        ready ? http::status::ok : http::status::service_unavailable, request.version()};
    response.set(http::field::server, runtime.server_name());
    response.set(http::field::content_type, "application/json");
    response.keep_alive(false);
    response.body() = Json{{"ready", ready},
                           {"reason", ready ? "capacity_available" : "admission_pressure"},
                           {"available_sequences", metrics.available_sequences},
                           {"available_kv_tokens", metrics.available_kv_tokens}}.dump();
    response.prepare_payload();
    stream.expires_after(std::chrono::seconds(5));
    boost::system::error_code ignored;
    (void)co_await http::async_write(stream, response,
                                    asio::redirect_error(asio::use_awaitable, ignored));
    co_return;
  }
  if (request.method() == http::verb::get && target == "/metrics") {
    const auto metrics = runtime.metrics_snapshot();
    const auto class_json = [](const ClassRuntimeMetrics& value) {
      return Json{{"admitted", value.admitted}, {"rejected", value.rejected},
                  {"rejected_invalid", value.rejected_invalid},
                  {"rejected_kv_capacity", value.rejected_kv_capacity},
                  {"rejected_sequence_capacity", value.rejected_sequence_capacity},
                  {"rejected_runtime_state", value.rejected_runtime_state},
                  {"resource_deferred", value.resource_deferred},
                  {"resource_unblocked", value.resource_unblocked},
                  {"completed", value.completed}, {"slo_met", value.slo_met},
                  {"service_tokens", value.service_tokens},
                  {"maximum_ready_wait_ms", value.maximum_ready_wait_ms},
                  {"scheduler_wait_p99_ms", value.scheduler_wait_p99_ms},
                  {"starvation_events", value.starvation_events},
                  {"hard_limit_aborted", value.hard_limit_aborted}};
    };
    const auto reason_name = [](AdaptivePrefillReason reason) -> std::string_view {
      switch (reason) {
        case AdaptivePrefillReason::StaticFallback: return "static_fallback";
        case AdaptivePrefillReason::HysteresisHold: return "hysteresis_hold";
        case AdaptivePrefillReason::ReduceForInteractivePressure: return "reduce_interactive_pressure";
        case AdaptivePrefillReason::IncreaseForIdleCapacity: return "increase_idle_capacity";
        case AdaptivePrefillReason::Cooldown: return "cooldown";
      }
      return "unknown";
    };
    const auto scheduler_name = metrics.scheduler == SchedulerMode::FIFO ? "fifo" : "sawds";
    Json adaptive_trace = Json::array();
    for (const auto& entry : metrics.adaptive_prefill_trace) {
      adaptive_trace.push_back({{"batch_id", entry.batch_id},
                                {"budget_before", entry.budget_before},
                                {"budget_after", entry.budget_after},
                                {"prefill_tokens", entry.prefill_tokens},
                                {"decode_tokens", entry.decode_tokens},
                                {"decode_backlog", entry.decode_backlog},
                                {"oldest_interactive_ready_wait_ms",
                                 entry.oldest_interactive_ready_wait_ms},
                                {"interactive_ttft_ewma_ms", entry.interactive_ttft_ewma_ms},
                                {"executor_idle_basis_points", entry.executor_idle_basis_points},
                                {"reason", reason_name(entry.reason)}});
    }
    Json pressure = "normal";
    switch (metrics.pressure) {
      case KvPressureState::Normal: pressure = "normal"; break;
      case KvPressureState::SoftPressure: pressure = "soft"; break;
      case KvPressureState::HighPressure: pressure = "high"; break;
      case KvPressureState::HardLimit: pressure = "hard"; break;
    }
    http::response<http::string_body> response{http::status::ok, request.version()};
    response.set(http::field::server, runtime.server_name());
    response.set(http::field::content_type, "application/json");
    response.keep_alive(false);
    response.body() = Json{
        {"scheduler", {{"mode", scheduler_name},
                        {"interactive", class_json(metrics.by_class[0])},
                        {"batch", class_json(metrics.by_class[1])},
                        {"eligible_requests", metrics.eligible_requests},
                        {"selected_requests", metrics.selected_requests},
                        {"last_batch_tokens", metrics.last_batch_tokens},
                        {"last_batch_sequences", metrics.last_batch_sequences},
                        {"last_batch_prefill_tokens", metrics.last_batch_prefill_tokens},
                        {"last_batch_decode_tokens", metrics.last_batch_decode_tokens},
                        {"last_epoch_interval_ns", metrics.last_scheduler_epoch_interval_ns},
                        {"last_epoch_duration_ns", metrics.last_scheduler_epoch_duration_ns},
                        {"last_backend_duration_ns", metrics.last_backend_duration_ns},
                        {"total_backend_duration_ns", metrics.total_backend_duration_ns},
                        {"total_prefill_tokens_issued", metrics.total_prefill_tokens_issued},
                        {"total_decode_tokens_issued", metrics.total_decode_tokens_issued},
                        {"adaptive_trace", std::move(adaptive_trace)},
                        {"event_queue_rejections", metrics.event_queue_rejections}}},
        {"adaptive_prefill", {{"enabled", metrics.adaptive_prefill_enabled},
                              {"epoch_budget_tokens", metrics.prefill_epoch_budget},
                              {"reason", reason_name(metrics.adaptive_prefill_reason)}}},
        {"batches_completed", metrics.completed_batches},
        {"kv", {{"units", "logical_tokens"},
                 {"pressure", std::move(pressure)},
                 {"hard_usable_tokens", metrics.hard_usable_kv_tokens},
                 {"available_tokens", metrics.available_kv_tokens},
                 {"available_sequences", metrics.available_sequences},
                 {"admission_ready", metrics.admission_ready},
                 {"charged_tokens", metrics.charged_kv_tokens},
                 {"committed_tokens", metrics.committed_kv_tokens},
                 {"soft_reserved_tokens", metrics.soft_reserved_kv_tokens},
                 {"authorized_tokens", metrics.authorized_kv_tokens},
                 {"peak_charged_tokens", metrics.peak_charged_kv_tokens},
                 {"released_tokens", metrics.released_kv_tokens},
                 {"rejected_reservations", metrics.rejected_kv_reservations}}}}.dump();
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
  if (!parse_request(request, payload, prompt, max_tokens, runtime.model_alias())) {
    co_await write_error(stream, request.version(), http::status::bad_request,
                         "expected strict streaming JSON for the configured backend",
                         runtime.server_name());
    co_return;
  }
  std::optional<ClockTime> deadline;
  if (!parse_deadline(request, deadline)) {
    co_await write_error(stream, request.version(), http::status::bad_request,
                         "invalid x-inferscale-deadline-ms", runtime.server_name());
    co_return;
  }
  RequestClass service_class = RequestClass::Interactive;
  std::optional<std::chrono::milliseconds> ttft_slo;
  std::optional<std::chrono::milliseconds> e2e_slo;
  if (!parse_request_class(request, service_class) ||
      !parse_slo_header(request, "x-inferscale-ttft-slo-ms", ttft_slo) ||
      !parse_slo_header(request, "x-inferscale-e2e-slo-ms", e2e_slo)) {
    co_await write_error(stream, request.version(), http::status::bad_request,
                         "invalid request class or SLO header", runtime.server_name());
    co_return;
  }
  const auto id = next_request_id();
  if (!id) {
    co_await write_error(stream, request.version(), http::status::service_unavailable,
                         "request ID space exhausted", runtime.server_name());
    co_return;
  }
  std::shared_ptr<const std::vector<TokenId>> prompt_token_ids;
  bool tokenization_failed = false;
  try {
    auto tokens = runtime.tokenize(prompt);
    if (tokens.empty() || tokens.size() > std::numeric_limits<std::uint32_t>::max()) {
      tokenization_failed = true;
    } else {
      prompt_token_ids = std::make_shared<const std::vector<TokenId>>(std::move(tokens));
    }
  } catch (const std::exception&) {
    tokenization_failed = true;
  }
  if (tokenization_failed) {
    co_await write_error(stream, request.version(), http::status::bad_request,
                         "prompt tokenization returned an invalid token count",
                         runtime.server_name());
    co_return;
  }
  auto output = std::make_shared<StreamBuffer>(output_buffer_bytes);
  auto admission = std::make_shared<std::atomic<AdmissionResult>>(AdmissionResult::Pending);
  const RequestKey key{*id, 1};
  RequestSpec spec{*id, static_cast<std::uint32_t>(prompt_token_ids->size()),
                   max_tokens, deadline,
                   static_cast<std::uint32_t>(output_buffer_bytes),
                   std::move(prompt_token_ids)};
  spec.service_class = service_class;
  spec.ttft_slo = ttft_slo;
  spec.e2e_slo = e2e_slo;
  NewRequestEvent event{std::move(spec), 1, output, admission};
  if (!runtime.submit(std::move(event))) {
    admission->store(AdmissionResult::Rejected, std::memory_order_release);
    co_await write_error(stream, request.version(), http::status::service_unavailable,
                         "runtime event queue is full", runtime.server_name());
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
                         "runtime admission rejected or timed out", runtime.server_name());
    co_return;
  }

  http::response<http::empty_body> response{http::status::ok, request.version()};
  response.set(http::field::server, runtime.server_name());
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

  struct PeerWatchState {
    std::atomic<bool> stopped{false};
    std::atomic<bool> disconnected{false};
  };
  struct StopPeerWatch {
    std::shared_ptr<PeerWatchState> state;
    ~StopPeerWatch() { state->stopped.store(true, std::memory_order_release); }
  };
  auto peer_watch = std::make_shared<PeerWatchState>();
  StopPeerWatch stop_peer_watch{peer_watch};
  auto arm_peer_watch = std::make_shared<std::function<void()>>();
  const std::weak_ptr<std::function<void()>> weak_arm = arm_peer_watch;
  *arm_peer_watch = [&stream, &runtime, key, peer_watch, weak_arm, connection_strand] {
    stream.socket().async_wait(tcp::socket::wait_read,
        asio::bind_executor(connection_strand, [&stream, &runtime, key, peer_watch, weak_arm](
            const boost::system::error_code& wait_error) {
          if (peer_watch->stopped.load(std::memory_order_acquire)) return;
          if (wait_error) {
            if (wait_error != asio::error::operation_aborted) {
              peer_watch->disconnected.store(true, std::memory_order_release);
              peer_watch->stopped.store(true, std::memory_order_release);
              runtime.cancel(key);
            }
            return;
          }
          char byte = 0;
          boost::system::error_code receive_error;
          const auto received = stream.socket().receive(asio::buffer(&byte, 1),
              asio::socket_base::message_peek, receive_error);
          if (received > 0 || receive_error == asio::error::eof || !receive_error ||
              (receive_error != asio::error::would_block && receive_error != asio::error::try_again)) {
            peer_watch->disconnected.store(true, std::memory_order_release);
            peer_watch->stopped.store(true, std::memory_order_release);
            runtime.cancel(key);
            return;
          }
          if (const auto arm = weak_arm.lock()) (*arm)();
        }));
  };
  (*arm_peer_watch)();

  for (;;) {
    if (peer_watch->disconnected.load(std::memory_order_acquire)) co_return;
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
  peer_watch->stopped.store(true, std::memory_order_release);
  arm_peer_watch.reset();

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

RuntimeService::RuntimeService(std::unique_ptr<InferenceEngine> engine,
                               std::string model_alias, std::string backend_name,
                               RuntimeConfig config)
    : capabilities_(require_capabilities(engine)),
      reactor_(clock_, capabilities_.max_sequences, 1024,
               capabilities_.context_limit, capabilities_.max_output_piece_bytes,
               std::move(config)),
      engine_(std::move(engine)),
      model_alias_(std::move(model_alias)), backend_name_(std::move(backend_name)),
      server_name_("InferScale-" + backend_name_) {
  if (capabilities_.max_sequences == 0 || capabilities_.max_batch_tokens == 0 ||
      capabilities_.context_limit == 0 || capabilities_.max_prefill_chunk_tokens == 0 ||
      model_alias_.empty() || backend_name_.empty()) {
    throw std::invalid_argument("runtime service requires a configured inference engine");
  }
  metrics_snapshot_.store(std::make_shared<const RuntimeMetricsSnapshot>(),
                          std::memory_order_release);
  executor_thread_ = std::thread([this] { executor_loop(); });
  runtime_thread_ = std::thread([this] { runtime_loop(); });
}

FakeRuntimeService::FakeRuntimeService(RuntimeConfig config)
    : RuntimeService(std::make_unique<FakeEngine>(EngineCapabilities{64, 128, 32768, false, 11}),
                     "fake", "fake", std::move(config)) {}

RuntimeService::~RuntimeService() {
  request_shutdown();
  if (!wait_for_shutdown(std::chrono::seconds(10))) std::_Exit(124);
  if (runtime_thread_.joinable()) runtime_thread_.join();
  if (executor_thread_.joinable()) executor_thread_.join();
}

std::vector<TokenId> RuntimeService::tokenize(std::string_view prompt) const {
  return engine_->tokenize(prompt);
}

bool RuntimeService::submit(NewRequestEvent event) {
  const auto admission = event.admission;
  const auto count_rejection = [this] {
    auto current = event_queue_rejections_.load(std::memory_order_relaxed);
    while (current != std::numeric_limits<std::uint64_t>::max() &&
           !event_queue_rejections_.compare_exchange_weak(
               current, current + 1, std::memory_order_relaxed)) {}
  };
  if (shutdown_requested_.load(std::memory_order_acquire) ||
      runtime_failed_.load(std::memory_order_acquire) ||
      runtime_stopped_.load(std::memory_order_acquire)) {
    if (admission) admission->store(AdmissionResult::Rejected, std::memory_order_release);
    count_rejection();
    return false;
  }
  const bool accepted = reactor_.post(std::move(event));
  if (!accepted) {
    if (admission) admission->store(AdmissionResult::Rejected, std::memory_order_release);
    count_rejection();
  }
  return accepted;
}

bool RuntimeService::post(RuntimeEvent event) { return reactor_.post(std::move(event)); }

void RuntimeService::cancel(RequestKey key) { (void)post(CancelEvent{key}); }

void RuntimeService::request_shutdown() {
  if (runtime_stopped_.load(std::memory_order_acquire)) return;
  if (!shutdown_requested_.exchange(true, std::memory_order_acq_rel)) {
    (void)reactor_.post(ShutdownEvent{});
  }
}

bool RuntimeService::wait_for_shutdown(std::chrono::milliseconds timeout) {
  std::unique_lock lock(state_mutex_);
  return state_changed_.wait_for(lock, timeout, [this] {
    return runtime_stopped_.load(std::memory_order_acquire) &&
           executor_stopped_.load(std::memory_order_acquire);
  });
}

void RuntimeService::notify_state_change() { state_changed_.notify_all(); }

RuntimeMetricsSnapshot RuntimeService::metrics_snapshot() const {
  const auto value = metrics_snapshot_.load(std::memory_order_acquire);
  return value ? *value : RuntimeMetricsSnapshot{};
}

void RuntimeService::runtime_loop() {
  bool batch_active = false;
  auto last_metrics_update = std::chrono::steady_clock::now();
  const auto update_metrics = [this] {
    auto snapshot = reactor_.metrics();
    snapshot.event_queue_rejections = event_queue_rejections_.load(std::memory_order_relaxed);
    metrics_snapshot_.store(std::make_shared<const RuntimeMetricsSnapshot>(std::move(snapshot)),
                            std::memory_order_release);
  };
  update_metrics();
  for (;;) {
    INFERSCALE_NVTX_RANGE("RuntimeService/ReactorCycle");
    reactor_.run_ready();
    reactor_.expire_deadlines();
    const auto metrics_now = std::chrono::steady_clock::now();
    if (metrics_now - last_metrics_update >= std::chrono::milliseconds(100)) {
      update_metrics();
      last_metrics_update = metrics_now;
    }
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
      const auto configured_chunk = reactor_.config().scheduler_prefill_chunk_tokens == 0
          ? capabilities_.max_prefill_chunk_tokens
          : reactor_.config().scheduler_prefill_chunk_tokens;
      const auto plan = reactor_.make_configured_plan(capabilities_.max_batch_tokens,
                                                      capabilities_.max_sequences,
                                                      std::min(configured_chunk,
                                                               capabilities_.max_batch_tokens));
      if (!plan.items.empty()) {
        auto ticket = reactor_.issue(plan);
        if (ticket) {
          outstanding_engine_commands_.fetch_add(1, std::memory_order_relaxed);
          if (!engine_commands_.push_wait(ExecuteCommand{std::move(*ticket)})) std::terminate();
          batch_active = true;
        }
      } else {
        (void)reactor_.resolve_resource_stall();
      }
    }

    if ((reactor_.shutdown_complete() || reactor_.fatal_error()) && !batch_active &&
        outstanding_engine_commands_.load(std::memory_order_acquire) == 0) {
      update_metrics();
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

void RuntimeService::executor_loop() {
  while (auto command = engine_commands_.wait_pop()) {
    std::visit([this](auto&& value) {
      using Command = std::decay_t<decltype(value)>;
      if constexpr (std::is_same_v<Command, ExecuteCommand>) {
        INFERSCALE_NVTX_RANGE("RuntimeService/ExecuteBatch");
        EngineResult result;
        const auto execution_started = std::chrono::steady_clock::now();
        try {
          result = engine_->execute(value.ticket);
        } catch (const std::exception& error) {
          result = {false, FailureClass::ProcessFatal, {}, error.what()};
        } catch (...) {
          result = {false, FailureClass::ProcessFatal, {}, "unknown engine exception"};
        }
        const auto backend_duration = std::chrono::steady_clock::now() - execution_started;
        if (!reactor_.post_completion(BatchCompletedEvent{
                std::move(value.ticket), std::move(result),
                std::chrono::duration_cast<std::chrono::nanoseconds>(backend_duration)})) {
          std::terminate();
        }
        outstanding_engine_commands_.fetch_sub(1, std::memory_order_release);
      } else if constexpr (std::is_same_v<Command, ClearCommand>) {
        INFERSCALE_NVTX_RANGE("RuntimeService/ClearSequence");
        bool cleared = false;
        try {
          cleared = engine_->clear_sequence(value.request.sequence);
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
                     RuntimeService& runtime, std::size_t output_buffer_bytes,
                     int socket_send_buffer_bytes)
    : context_(context), acceptor_(context), accept_strand_(asio::make_strand(context)), runtime_(runtime),
      active_connections_(std::make_shared<std::atomic<std::size_t>>(0)),
      output_buffer_bytes_(output_buffer_bytes), socket_send_buffer_bytes_(socket_send_buffer_bytes) {
  if (output_buffer_bytes_ == 0 ||
      output_buffer_bytes_ > std::numeric_limits<std::uint32_t>::max() ||
      socket_send_buffer_bytes_ < 0) {
    throw std::invalid_argument("invalid SSE server buffer configuration");
  }
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
  asio::post(accept_strand_, [weak] {
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
  acceptor_.async_accept(asio::bind_executor(accept_strand_,
      [weak](boost::system::error_code error, tcp::socket socket) {
    const auto self = weak.lock();
    if (!self) return;
    if (!error && !self->stopped_.load(std::memory_order_acquire)) {
      const auto active = self->active_connections_->fetch_add(1, std::memory_order_relaxed) + 1;
      if (active > kMaxConnections) {
        self->active_connections_->fetch_sub(1, std::memory_order_relaxed);
        boost::system::error_code ignored;
        socket.close(ignored);
      } else {
        if (self->socket_send_buffer_bytes_ > 0) {
          socket.set_option(asio::socket_base::send_buffer_size(self->socket_send_buffer_bytes_), error);
        }
        if (error) {
          self->active_connections_->fetch_sub(1, std::memory_order_relaxed);
          boost::system::error_code ignored;
          socket.close(ignored);
        } else {
          auto connection_strand = asio::make_strand(self->context_);
          asio::co_spawn(connection_strand,
                         serve_connection(std::move(socket), self->runtime_,
                                          self->active_connections_, connection_strand,
                                          self->output_buffer_bytes_),
                         asio::detached);
        }
      }
    }
    if (!self->stopped_.load(std::memory_order_acquire)) self->accept_next();
  }));
}

}  // namespace inferscale::api
