#include "inferscale/api/fake_server.hpp"
#include "inferscale/engine/llama_engine.hpp"

#include <boost/asio/connect.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/read.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace asio = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;
using tcp = asio::ip::tcp;

namespace {

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

std::string completion_body(std::string prompt, std::uint32_t max_tokens) {
  return nlohmann::json{{"model", "local-model"}, {"prompt", std::move(prompt)},
                        {"max_tokens", max_tokens}, {"stream", true},
                        {"stream_options", {{"include_usage", true}}}}.dump();
}

http::response<http::string_body> complete(std::uint16_t port, std::string prompt,
                                           std::uint32_t max_tokens) {
  asio::io_context context;
  beast::tcp_stream stream(context);
  stream.expires_after(std::chrono::seconds(10));
  stream.connect({asio::ip::address_v4::loopback(), port});
  http::request<http::string_body> request{http::verb::post, "/v1/completions", 11};
  request.set(http::field::host, "127.0.0.1");
  request.set(http::field::content_type, "application/json");
  request.body() = completion_body(std::move(prompt), max_tokens);
  request.prepare_payload();
  http::write(stream, request);
  beast::flat_buffer buffer;
  http::response<http::string_body> response;
  http::read(stream, buffer, response);
  return response;
}

}  // namespace

int main() {
  const auto* model_path = std::getenv("INFERSCALE_TEST_MODEL");
  if (model_path == nullptr || *model_path == '\0') {
    std::cout << "SKIP: set INFERSCALE_TEST_MODEL to run the model-backed slow-reader test\n";
    return 77;
  }

  asio::io_context context(4);
  std::unique_ptr<inferscale::api::RuntimeService> runtime;
  std::shared_ptr<inferscale::api::SseServer> server;
  std::vector<std::thread> workers;
  int result_code = 0;
  try {
    inferscale::LlamaEngineConfig config;
    config.model_path = model_path;
    if (const auto* layers = std::getenv("INFERSCALE_TEST_GPU_LAYERS")) {
      config.gpu_layers = std::stoi(layers);
    }
    config.context_per_sequence = 2048;
    config.max_sequences = 1;
    config.batch_tokens = 512;
    config.ubatch_tokens = 128;
    config.threads = 4;
    config.batch_threads = 4;
    auto engine = std::make_unique<inferscale::LlamaEngine>(config);
    const auto caps = engine->capabilities();
    require(caps.cuda_enabled || config.gpu_layers == 0, "requested CUDA backend is unavailable");
    const auto stream_capacity = static_cast<std::size_t>(caps.max_output_piece_bytes) * 2U;
    runtime = std::make_unique<inferscale::api::RuntimeService>(
        std::move(engine), "local-model", "llama.cpp");
    server = std::make_shared<inferscale::api::SseServer>(
        context, tcp::endpoint(asio::ip::address_v4::loopback(), 0), *runtime,
        stream_capacity, 1024);
    server->start();
    for (int index = 0; index < 4; ++index) workers.emplace_back([&context] { context.run(); });

    asio::io_context slow_context;
    beast::tcp_stream slow_reader(slow_context);
    slow_reader.socket().open(tcp::v4());
    slow_reader.socket().set_option(asio::socket_base::receive_buffer_size(1024));
    slow_reader.expires_after(std::chrono::seconds(10));
    slow_reader.connect({asio::ip::address_v4::loopback(), server->port()});
    http::request<http::string_body> request{http::verb::post, "/v1/completions", 11};
    request.set(http::field::host, "127.0.0.1");
    request.set(http::field::content_type, "application/json");
    request.body() = completion_body(
        "<|system|>\nYou are a helpful assistant.\n<|user|>\nSay exactly: Paris.\n<|assistant|>\n", 128);
    request.prepare_payload();
    const auto started = std::chrono::steady_clock::now();
    http::write(slow_reader, request);
    std::string response_header;
    char byte = 0;
    while (!response_header.ends_with("\r\n\r\n") && response_header.size() < 8192) {
      asio::read(slow_reader.socket(), asio::buffer(&byte, 1));
      response_header.push_back(byte);
    }
    require(response_header.starts_with("HTTP/1.1 200"), "slow-reader request was not accepted");

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(9);
    while (server->active_connections() != 0 && std::chrono::steady_clock::now() < deadline) {
      std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    if (elapsed < std::chrono::milliseconds(4500)) {
      std::cerr << "slow-reader ended after "
                << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
                << "ms while active_connections=" << server->active_connections() << '\n';
    }
    require(server->active_connections() == 0,
            "permanently slow reader did not reach the bounded socket write timeout");
    require(elapsed >= std::chrono::milliseconds(4500),
            "slow-reader connection ended before the documented five second write timeout");
    slow_reader.socket().close();
    require(runtime->healthy(), "slow-reader cancellation damaged model service health");

    const auto response = complete(server->port(),
        "<|system|>\nYou are a helpful assistant.\n<|user|>\nReply with one word: hello.\n<|assistant|>\n", 32);
    require(response.result() == http::status::ok, "request after slow-reader timeout failed");
    std::string output_text;
    std::uint32_t output_tokens = 0;
    std::uint32_t done_events = 0;
    std::istringstream events(response.body());
    for (std::string line; std::getline(events, line);) {
      if (!line.starts_with("data: ")) continue;
      const auto data = std::string_view(line).substr(6);
      if (data == "[DONE]") {
        ++done_events;
        continue;
      }
      const auto event = nlohmann::json::parse(data);
      for (const auto& choice : event.value("choices", nlohmann::json::array())) {
        output_text += choice.value("text", std::string{});
      }
      if (event.contains("usage")) output_tokens = event["usage"]["completion_tokens"].get<std::uint32_t>();
    }
    if (output_text != "Hey!" || output_tokens != 3 || done_events != 1) {
      std::cerr << "followup response after slow reader: " << response.body() << '\n';
      throw std::runtime_error("sequence cleanup or model output failed after slow-reader timeout");
    }
    std::cout << "MODEL SLOW READER PASS: status=200, write_timeout_ms="
              << std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count()
              << ", test_stream_capacity_bytes=" << stream_capacity
              << ", socket_send_buffer_bytes=1024, followup=Hey!, completion_tokens=3\n";
  } catch (const boost::system::system_error& error) {
    const auto condition = error.code().default_error_condition();
    if (condition == boost::system::errc::make_error_condition(boost::system::errc::operation_not_permitted) ||
        condition == boost::system::errc::make_error_condition(boost::system::errc::address_not_available)) {
      std::cout << "SKIP: local loopback sockets unavailable: " << error.what() << '\n';
      result_code = 77;
    } else {
      std::cerr << error.what() << '\n';
      result_code = 1;
    }
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    result_code = 1;
  }

  if (server) server->stop();
  if (runtime) {
    runtime->request_shutdown();
    if (!runtime->wait_for_shutdown(std::chrono::seconds(10))) result_code = 1;
  }
  context.stop();
  for (auto& worker : workers) if (worker.joinable()) worker.join();
  return result_code;
}
