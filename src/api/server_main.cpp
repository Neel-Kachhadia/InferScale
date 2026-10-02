#include "inferscale/api/fake_server.hpp"

#include <boost/asio/signal_set.hpp>
#include <boost/asio/steady_timer.hpp>

#include <charconv>
#include <csignal>
#include <cstdlib>
#include <chrono>
#include <iostream>
#include <memory>
#include <string_view>

namespace {

using ShutdownTimer = boost::asio::steady_timer;

void monitor_shutdown(const std::shared_ptr<inferscale::api::SseServer>& server,
                      inferscale::api::FakeRuntimeService& runtime,
                      const std::shared_ptr<ShutdownTimer>& timer,
                      std::chrono::steady_clock::time_point deadline) {
  if (runtime.wait_for_shutdown(std::chrono::milliseconds(0)) &&
      server->active_connections() == 0) return;
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
  std::string_view host = "127.0.0.1";
  unsigned short port = 8080;
  for (int i = 1; i < argc; ++i) {
    const std::string_view argument(argv[i]);
    if ((argument == "--host" || argument == "--port") && i + 1 >= argc) {
      std::cerr << "missing value for " << argument << '\n';
      return 2;
    }
    if (argument == "--host") {
      host = argv[++i];
    } else if (argument == "--port") {
      unsigned int parsed = 0;
      const std::string_view value(argv[++i]);
      const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), parsed);
      if (error != std::errc{} || end != value.data() + value.size() || parsed == 0 || parsed > 65535) {
        std::cerr << "invalid port\n";
        return 2;
      }
      port = static_cast<unsigned short>(parsed);
    } else {
      std::cerr << "usage: inferscale_fake_server [--host ADDRESS] [--port PORT]\n";
      return 2;
    }
  }

  try {
    boost::asio::io_context context(1);
    inferscale::api::FakeRuntimeService runtime;
    auto server = std::make_shared<inferscale::api::SseServer>(context,
        boost::asio::ip::tcp::endpoint{boost::asio::ip::make_address(host), port}, runtime);
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
    std::cerr << "InferScale fake backend listening on " << host << ':' << server->port() << '\n';
    context.run();
    if (!runtime.wait_for_shutdown(std::chrono::milliseconds(0))) {
      std::cerr << "runtime exceeded the 10 second shutdown deadline; hard stop\n";
      std::_Exit(124);
    }
  } catch (const std::exception& error) {
    std::cerr << "inferscale_fake_server: " << error.what() << '\n';
    return 1;
  }
  return 0;
}
