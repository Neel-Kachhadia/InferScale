#pragma once

#include "inferscale/engine/fake_engine.hpp"
#include "inferscale/runtime/runtime_reactor.hpp"

#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>
#include <variant>

namespace inferscale::api {

class FakeRuntimeService {
 public:
  FakeRuntimeService();
  ~FakeRuntimeService();
  FakeRuntimeService(const FakeRuntimeService&) = delete;
  FakeRuntimeService& operator=(const FakeRuntimeService&) = delete;

  bool submit(NewRequestEvent event);
  bool post(RuntimeEvent event);
  void cancel(RequestKey key);
  void request_shutdown();
  bool wait_for_shutdown(std::chrono::milliseconds timeout);
  void pause_engine_before_execute() { engine_.pause_before_execute(); }
  bool wait_until_engine_paused(std::chrono::milliseconds timeout) {
    return engine_.wait_until_before_execute_paused(timeout);
  }
  void resume_engine() { engine_.resume(); }
  void fail_next_engine(FailureClass failure) { engine_.fail_next(failure); }
  bool healthy() const noexcept {
    return !shutdown_requested_.load(std::memory_order_acquire) &&
           !runtime_failed_.load(std::memory_order_acquire) &&
           !runtime_stopped_.load(std::memory_order_acquire);
  }

 private:
  struct ExecuteCommand { BatchTicket ticket; };
  struct ClearCommand { ClearRequest request; };
  struct StopCommand {};
  using EngineCommand = std::variant<ExecuteCommand, ClearCommand, StopCommand>;

  void runtime_loop();
  void executor_loop();
  void notify_state_change();

  SteadyClock clock_;
  RuntimeReactor reactor_;
  FakeEngine engine_;
  BoundedQueue<EngineCommand> engine_commands_{128};
  std::thread runtime_thread_;
  std::thread executor_thread_;
  std::atomic<bool> shutdown_requested_{false};
  std::atomic<bool> runtime_stopped_{false};
  std::atomic<bool> executor_stopped_{false};
  std::atomic<bool> runtime_failed_{false};
  // Covers queued and executing commands until their completion event is enqueued.
  std::atomic<std::size_t> outstanding_engine_commands_{0};
  std::mutex state_mutex_;
  std::condition_variable state_changed_;
};

class SseServer : public std::enable_shared_from_this<SseServer> {
 public:
  SseServer(boost::asio::io_context& context, const boost::asio::ip::tcp::endpoint& endpoint,
            FakeRuntimeService& runtime);
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
  FakeRuntimeService& runtime_;
  std::shared_ptr<std::atomic<std::size_t>> active_connections_;
  std::atomic<bool> stopped_{false};
  bool started_{false};
};

}  // namespace inferscale::api
