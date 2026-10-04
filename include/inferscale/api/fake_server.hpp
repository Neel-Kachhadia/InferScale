#pragma once

#include "inferscale/engine/fake_engine.hpp"
#include "inferscale/execution/inference_engine.hpp"
#include "inferscale/runtime/runtime_reactor.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/strand.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <mutex>
#include <memory>
#include <string>
#include <string_view>
#include <thread>
#include <variant>
#include <vector>

namespace inferscale::api {

class RuntimeService {
 public:
  RuntimeService(std::unique_ptr<InferenceEngine> engine, std::string model_alias,
                 std::string backend_name, RuntimeConfig config = {});
  virtual ~RuntimeService();
  RuntimeService(const RuntimeService&) = delete;
  RuntimeService& operator=(const RuntimeService&) = delete;

  bool submit(NewRequestEvent event);
  bool post(RuntimeEvent event);
  std::vector<TokenId> tokenize(std::string_view prompt) const;
  const std::string& model_alias() const noexcept { return model_alias_; }
  const std::string& backend_name() const noexcept { return backend_name_; }
  const std::string& server_name() const noexcept { return server_name_; }
  void cancel(RequestKey key);
  void request_shutdown();
  bool wait_for_shutdown(std::chrono::milliseconds timeout);
  RuntimeMetricsSnapshot metrics_snapshot() const;
  bool ready_for_admission() const noexcept {
    const auto snapshot = metrics_snapshot_.load(std::memory_order_acquire);
    return healthy() && snapshot && snapshot->admission_ready;
  }
  bool healthy() const noexcept {
    return !shutdown_requested_.load(std::memory_order_acquire) &&
           !runtime_failed_.load(std::memory_order_acquire) &&
           !runtime_stopped_.load(std::memory_order_acquire);
  }

 protected:
  InferenceEngine& engine() noexcept { return *engine_; }

 private:
  struct ExecuteCommand { BatchTicket ticket; };
  struct ClearCommand { ClearRequest request; };
  struct StopCommand {};
  using EngineCommand = std::variant<ExecuteCommand, ClearCommand, StopCommand>;

  void runtime_loop();
  void executor_loop();
  void notify_state_change();

  SteadyClock clock_;
  EngineCapabilities capabilities_;
  RuntimeReactor reactor_;
  std::unique_ptr<InferenceEngine> engine_;
  std::string model_alias_;
  std::string backend_name_;
  std::string server_name_;
  BoundedQueue<EngineCommand> engine_commands_;
  std::thread runtime_thread_;
  std::thread executor_thread_;
  std::atomic<bool> shutdown_requested_{false};
  std::atomic<bool> runtime_stopped_{false};
  std::atomic<bool> executor_stopped_{false};
  std::atomic<bool> runtime_failed_{false};
  std::atomic<std::uint64_t> event_queue_rejections_{0};
  std::atomic<std::shared_ptr<const RuntimeMetricsSnapshot>> metrics_snapshot_;
  // Covers queued and executing commands until their completion event is enqueued.
  std::atomic<std::size_t> outstanding_engine_commands_{0};
  std::mutex state_mutex_;
  std::condition_variable state_changed_;
};

class FakeRuntimeService final : public RuntimeService {
 public:
  explicit FakeRuntimeService(RuntimeConfig config = {});
  void pause_engine_before_execute() { fake_engine().pause_before_execute(); }
  bool wait_until_engine_paused(std::chrono::milliseconds timeout) {
    return fake_engine().wait_until_before_execute_paused(timeout);
  }
  void resume_engine() { fake_engine().resume(); }
  void fail_next_engine(FailureClass failure) { fake_engine().fail_next(failure); }

 private:
  FakeEngine& fake_engine() noexcept { return static_cast<FakeEngine&>(engine()); }
};

class SseServer : public std::enable_shared_from_this<SseServer> {
 public:
  SseServer(boost::asio::io_context& context, const boost::asio::ip::tcp::endpoint& endpoint,
            RuntimeService& runtime, std::size_t output_buffer_bytes = 4096,
            int socket_send_buffer_bytes = 0);
  ~SseServer();
  SseServer(const SseServer&) = delete;
  SseServer& operator=(const SseServer&) = delete;

  void start();
  void stop();
  std::uint16_t port() const;
  std::size_t active_connections() const noexcept {
    return active_connections_->load(std::memory_order_relaxed);
  }

 private:
  void accept_next();

  boost::asio::io_context& context_;
  boost::asio::ip::tcp::acceptor acceptor_;
  boost::asio::strand<boost::asio::io_context::executor_type> accept_strand_;
  RuntimeService& runtime_;
  std::shared_ptr<std::atomic<std::size_t>> active_connections_;
  std::size_t output_buffer_bytes_;
  int socket_send_buffer_bytes_;
  std::atomic<bool> stopped_{false};
  bool started_{false};
};

}  // namespace inferscale::api
