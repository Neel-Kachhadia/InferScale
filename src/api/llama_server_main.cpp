#include "inferscale/api/fake_server.hpp"
#include "inferscale/engine/llama_engine.hpp"

#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>

#include <charconv>
#include <chrono>
#include <csignal>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

using ShutdownTimer = boost::asio::steady_timer;

struct Options {
  std::string model;
  std::string host{"127.0.0.1"};
  unsigned short port{8080};
  inferscale::LlamaEngineConfig engine;
  inferscale::RuntimeConfig runtime;
  std::uint32_t http_threads{4};
};

template <typename Integer>
Integer parse_integer(std::string_view text, Integer minimum, Integer maximum,
                      std::string_view option) {
  Integer value{};
  const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value < minimum || value > maximum) {
    throw std::invalid_argument("invalid value for " + std::string(option));
  }
  return value;
}

Options parse_options(int argc, char** argv) {
  Options options;
  bool has_model = false;
  bool prefill_epoch_set = false;
  bool adaptive_initial_set = false;
  for (int index = 1; index < argc; ++index) {
    const std::string_view option(argv[index]);
    if (option == "--help" || option == "-h") {
      std::cout << "usage: inferscale_llama_server --model GGUF [--host ADDRESS] [--port PORT] "
                   "[--ctx-size-per-seq TOKENS] [--parallel N] [--batch-size TOKENS] "
                   "[--ubatch-size TOKENS] [--gpu-layers N] [--threads N] "
                   "[--threads-batch N] [--threads-http N] [--scheduler fifo|sawds] "
                   "[--hard-kv-tokens TOKENS] [--prefill-epoch-tokens TOKENS] "
                   "[--generation-reserve-tokens TOKENS] "
                   "[--scheduler-prefill-chunk TOKENS] "
                   "[--adaptive-prefill] [--adaptive-min TOKENS] [--adaptive-max TOKENS] "
                   "[--adaptive-initial TOKENS] [--adaptive-step TOKENS] "
                   "[--adaptive-high-wait-ms MS] [--adaptive-low-wait-ms MS] "
                   "[--adaptive-high-decode-backlog N] [--adaptive-low-decode-backlog N] "
                   "[--adaptive-windows N] [--adaptive-cooldown N]\n";
      std::exit(0);
    }
    if (option == "--adaptive-prefill") {
      options.runtime.adaptive_prefill.enabled = true;
      continue;
    }
    if (option == "--static-prefill") {
      options.runtime.adaptive_prefill.enabled = false;
      continue;
    }
    if (index + 1 >= argc) throw std::invalid_argument("missing value for " + std::string(option));
    const std::string_view value(argv[++index]);
    if (option == "--model") {
      options.model = value;
      has_model = true;
    } else if (option == "--host") {
      options.host = value;
    } else if (option == "--port") {
      options.port = parse_integer<unsigned short>(value, 1, 65535, option);
    } else if (option == "--ctx-size-per-seq") {
      options.engine.context_per_sequence = parse_integer<std::uint32_t>(value, 1, 1'000'000, option);
    } else if (option == "--parallel") {
      options.engine.max_sequences = parse_integer<std::uint32_t>(value, 1, 4096, option);
    } else if (option == "--batch-size") {
      options.engine.batch_tokens = parse_integer<std::uint32_t>(value, 1, 1'000'000, option);
    } else if (option == "--ubatch-size") {
      options.engine.ubatch_tokens = parse_integer<std::uint32_t>(value, 1, 1'000'000, option);
    } else if (option == "--gpu-layers") {
      options.engine.gpu_layers = parse_integer<std::int32_t>(value, -1, 10000, option);
    } else if (option == "--threads") {
      options.engine.threads = parse_integer<std::int32_t>(value, 1, 4096, option);
    } else if (option == "--threads-batch") {
      options.engine.batch_threads = parse_integer<std::int32_t>(value, 1, 4096, option);
    } else if (option == "--threads-http") {
      options.http_threads = parse_integer<std::uint32_t>(value, 1, 256, option);
    } else if (option == "--scheduler") {
      if (value == "fifo") options.runtime.scheduler = inferscale::SchedulerMode::FIFO;
      else if (value == "sawds") options.runtime.scheduler = inferscale::SchedulerMode::SAWDS;
      else throw std::invalid_argument("--scheduler must be fifo or sawds");
    } else if (option == "--hard-kv-tokens") {
      options.runtime.hard_usable_kv_tokens = parse_integer<std::uint64_t>(
          value, 1, std::numeric_limits<std::uint64_t>::max(), option);
    } else if (option == "--generation-reserve-tokens") {
      options.runtime.admission_generation_reserve_tokens = parse_integer<std::uint32_t>(
          value, 0, std::numeric_limits<std::uint32_t>::max(), option);
    } else if (option == "--prefill-epoch-tokens") {
      options.runtime.max_prefill_tokens_per_epoch = parse_integer<std::uint32_t>(
          value, 1, std::numeric_limits<std::uint32_t>::max(), option);
      prefill_epoch_set = true;
    } else if (option == "--scheduler-prefill-chunk") {
      options.runtime.scheduler_prefill_chunk_tokens = parse_integer<std::uint32_t>(
          value, 1, 1'000'000, option);
    } else if (option == "--adaptive-min") {
      options.runtime.adaptive_prefill.minimum_tokens = parse_integer<std::uint32_t>(
          value, 1, std::numeric_limits<std::uint32_t>::max(), option);
    } else if (option == "--adaptive-max") {
      options.runtime.adaptive_prefill.maximum_tokens = parse_integer<std::uint32_t>(
          value, 1, std::numeric_limits<std::uint32_t>::max(), option);
    } else if (option == "--adaptive-initial") {
      options.runtime.adaptive_prefill.initial_tokens = parse_integer<std::uint32_t>(
          value, 1, std::numeric_limits<std::uint32_t>::max(), option);
      adaptive_initial_set = true;
    } else if (option == "--adaptive-step") {
      options.runtime.adaptive_prefill.step_tokens = parse_integer<std::uint32_t>(
          value, 1, std::numeric_limits<std::uint32_t>::max(), option);
    } else if (option == "--adaptive-high-wait-ms") {
      options.runtime.adaptive_prefill.high_interactive_wait_ms = parse_integer<std::uint32_t>(
          value, 1, std::numeric_limits<std::uint32_t>::max(), option);
    } else if (option == "--adaptive-low-wait-ms") {
      options.runtime.adaptive_prefill.low_interactive_wait_ms = parse_integer<std::uint32_t>(
          value, 0, std::numeric_limits<std::uint32_t>::max(), option);
    } else if (option == "--adaptive-high-decode-backlog") {
      options.runtime.adaptive_prefill.high_decode_backlog = parse_integer<std::uint32_t>(
          value, 1, std::numeric_limits<std::uint32_t>::max(), option);
    } else if (option == "--adaptive-low-decode-backlog") {
      options.runtime.adaptive_prefill.low_decode_backlog = parse_integer<std::uint32_t>(
          value, 0, std::numeric_limits<std::uint32_t>::max(), option);
    } else if (option == "--adaptive-windows") {
      options.runtime.adaptive_prefill.windows_before_change = parse_integer<std::uint32_t>(
          value, 1, std::numeric_limits<std::uint32_t>::max(), option);
    } else if (option == "--adaptive-cooldown") {
      options.runtime.adaptive_prefill.cooldown_windows = parse_integer<std::uint32_t>(
          value, 0, std::numeric_limits<std::uint32_t>::max(), option);
    } else {
      throw std::invalid_argument("unknown option " + std::string(option));
    }
  }
  if (!has_model) throw std::invalid_argument("--model is required");
  if (options.engine.ubatch_tokens > options.engine.batch_tokens) {
    throw std::invalid_argument("--ubatch-size cannot exceed --batch-size");
  }
  if (prefill_epoch_set && !adaptive_initial_set) {
    options.runtime.adaptive_prefill.initial_tokens =
        options.runtime.max_prefill_tokens_per_epoch;
  }
  options.engine.model_path = std::move(options.model);
  return options;
}

void monitor_shutdown(const std::shared_ptr<inferscale::api::SseServer>& server,
                      inferscale::api::RuntimeService& runtime,
                      const std::shared_ptr<ShutdownTimer>& timer,
                      std::chrono::steady_clock::time_point deadline) {
  if (runtime.wait_for_shutdown(std::chrono::milliseconds(0)) && server->active_connections() == 0) return;
  if (std::chrono::steady_clock::now() >= deadline) {
    std::cerr << "shutdown exceeded the 10 second deadline; hard stop\n";
    std::_Exit(124);
  }
  timer->expires_after(std::chrono::milliseconds(10));
  timer->async_wait([server, &runtime, timer, deadline](const boost::system::error_code& error) {
    if (!error) monitor_shutdown(server, runtime, timer, deadline);
  });
}

}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_options(argc, argv);
    const auto total_context = static_cast<std::uint64_t>(options.engine.context_per_sequence) *
                               options.engine.max_sequences;
    if (total_context > std::numeric_limits<std::uint32_t>::max()) {
      throw std::invalid_argument("resolved total context exceeds llama.cpp limits");
    }

    boost::asio::io_context context(static_cast<int>(options.http_threads));
    auto engine = std::make_unique<inferscale::LlamaEngine>(options.engine);
    const auto capabilities = engine->capabilities();
    inferscale::api::RuntimeService runtime(std::move(engine), "local-model", "llama.cpp",
                                            options.runtime);
    auto server = std::make_shared<inferscale::api::SseServer>(context,
        boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address(options.host), options.port}, runtime);
    server->start();
    boost::asio::signal_set signals(context, SIGINT, SIGTERM);
    auto shutdown_timer = std::make_shared<ShutdownTimer>(context);
    signals.async_wait([&](const boost::system::error_code& error, int) {
      if (!error) {
        server->stop();
        runtime.request_shutdown();
        monitor_shutdown(server, runtime, shutdown_timer,
                         std::chrono::steady_clock::now() + std::chrono::seconds(10));
      }
    });

    const auto total_resolved = capabilities.resolved_total_context;
    std::cerr << "InferScale llama.cpp backend listening on " << options.host << ':' << server->port() << '\n'
              << "model=" << options.engine.model_path << '\n'
              << "context_per_sequence_requested=" << options.engine.context_per_sequence
              << " context_per_sequence_resolved=" << capabilities.context_limit
              << " total_context_requested=" << total_context
              << " total_context_resolved=" << total_resolved << '\n'
              << "parallel_requested=" << options.engine.max_sequences
              << " parallel_resolved=" << capabilities.max_sequences
              << " batch_size_requested=" << options.engine.batch_tokens
              << " batch_size_resolved=" << capabilities.max_batch_tokens
              << " ubatch_size_requested=" << options.engine.ubatch_tokens
              << " ubatch_size_resolved=" << capabilities.max_prefill_chunk_tokens
              << " gpu_layers=" << options.engine.gpu_layers
              << " cuda_backend_available=" << (capabilities.cuda_enabled ? "true" : "false") << '\n'
              << "threads=" << options.engine.threads
              << " threads_batch=" << options.engine.batch_threads
              << " threads_http=" << options.http_threads
              << " sampling=greedy kv_unified=false"
              << " scheduler=" << (options.runtime.scheduler == inferscale::SchedulerMode::FIFO
                                        ? "fifo" : "sawds")
              << " kv_hard_tokens_requested="
              << (options.runtime.hard_usable_kv_tokens == 0
                      ? std::string_view("auto_90pct_logical_context")
                      : std::string_view("configured"))
              << ":" << options.runtime.hard_usable_kv_tokens
              << " generation_reserve_tokens="
              << options.runtime.admission_generation_reserve_tokens
              << " prefill_epoch_tokens=" << options.runtime.max_prefill_tokens_per_epoch
              << " scheduler_prefill_chunk_tokens="
              << options.runtime.scheduler_prefill_chunk_tokens
              << " adaptive_prefill=" << (options.runtime.adaptive_prefill.enabled ? "true" : "false")
              << '\n';

    std::vector<std::thread> http_workers;
    http_workers.reserve(options.http_threads > 0 ? options.http_threads - 1 : 0);
    for (std::uint32_t worker = 1; worker < options.http_threads; ++worker) {
      http_workers.emplace_back([&context] { context.run(); });
    }
    context.run();
    for (auto& worker : http_workers) worker.join();
    if (!runtime.wait_for_shutdown(std::chrono::milliseconds(0))) {
      std::cerr << "runtime exceeded the 10 second shutdown deadline; hard stop\n";
      std::_Exit(124);
    }
  } catch (const std::exception& error) {
    std::cerr << "inferscale_llama_server: " << error.what() << '\n';
    return 2;
  }
  return 0;
}
