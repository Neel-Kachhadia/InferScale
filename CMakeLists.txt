cmake_minimum_required(VERSION 3.25)
project(InferScale VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
find_package(Boost 1.83.0 EXACT REQUIRED COMPONENTS system)
find_package(GTest 1.14.0 EXACT REQUIRED)
find_package(nlohmann_json 3.11.3 EXACT REQUIRED)
find_package(Threads REQUIRED)
option(INFERSCALE_ENABLE_SANITIZERS "Enable ASan and UBSan" OFF)
option(INFERSCALE_ENABLE_TSAN "Enable TSan" OFF)
option(INFERSCALE_WITH_LLAMA "Fetch and build the pinned llama.cpp backend" OFF)

if(INFERSCALE_ENABLE_SANITIZERS AND INFERSCALE_ENABLE_TSAN)
  message(FATAL_ERROR "ASan/UBSan and TSan must use separate builds")
endif()

add_library(inferscale_runtime
  src/runtime/runtime_reactor.cpp
  src/runtime/sequence_allocator.cpp
  src/runtime/lifecycle.cpp
  src/engine/fake_engine.cpp)
target_include_directories(inferscale_runtime PUBLIC include)
target_compile_features(inferscale_runtime PUBLIC cxx_std_20)
target_compile_options(inferscale_runtime PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
target_link_libraries(inferscale_runtime PUBLIC Threads::Threads)
if(INFERSCALE_ENABLE_SANITIZERS)
  target_compile_options(inferscale_runtime PUBLIC -fsanitize=address,undefined -fno-omit-frame-pointer)
  target_link_options(inferscale_runtime PUBLIC -fsanitize=address,undefined)
elseif(INFERSCALE_ENABLE_TSAN)
  target_compile_options(inferscale_runtime PUBLIC -fsanitize=thread -fno-omit-frame-pointer)
  target_link_options(inferscale_runtime PUBLIC -fsanitize=thread)
endif()

add_library(inferscale_http src/api/fake_server.cpp)
target_include_directories(inferscale_http PUBLIC include)
target_compile_features(inferscale_http PUBLIC cxx_std_20)
target_compile_options(inferscale_http PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
target_link_libraries(inferscale_http PUBLIC inferscale_runtime Boost::system nlohmann_json::nlohmann_json)

add_executable(inferscale_fake_server src/api/server_main.cpp)
target_link_libraries(inferscale_fake_server PRIVATE inferscale_http)

if(BUILD_TESTING)
  add_executable(inferscale_runtime_tests tests/runtime_test.cpp)
  target_link_libraries(inferscale_runtime_tests PRIVATE inferscale_runtime GTest::gtest_main)
  target_compile_options(inferscale_runtime_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  add_test(NAME runtime COMMAND inferscale_runtime_tests)

  add_executable(inferscale_http_tests tests/http_server_test.cpp)
  target_link_libraries(inferscale_http_tests PRIVATE inferscale_http)
  target_compile_options(inferscale_http_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  add_test(NAME http_server COMMAND inferscale_http_tests)
  set_tests_properties(http_server PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 20)
endif()

if(INFERSCALE_WITH_LLAMA)
  include(cmake/Dependencies.cmake)
  inferscale_fetch_llama()

  add_library(inferscale_llama
    src/engine/llama_batch_builder.cpp
    src/engine/llama_engine.cpp)
  target_include_directories(inferscale_llama PUBLIC include)
  target_compile_features(inferscale_llama PUBLIC cxx_std_20)
  target_compile_options(inferscale_llama PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
  target_link_libraries(inferscale_llama PUBLIC inferscale_runtime llama)

  add_executable(inferscale_llama_server src/api/llama_server_main.cpp)
  target_link_libraries(inferscale_llama_server PRIVATE inferscale_http inferscale_llama)
  target_compile_options(inferscale_llama_server PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)

  add_executable(inferscale_llama_smoke src/engine/llama_smoke.cpp)
  target_link_libraries(inferscale_llama_smoke PRIVATE llama)
  target_compile_features(inferscale_llama_smoke PRIVATE cxx_std_20)

  if(BUILD_TESTING)
    add_executable(inferscale_llama_tests tests/llama_engine_test.cpp)
    target_link_libraries(inferscale_llama_tests PRIVATE inferscale_llama GTest::gtest_main)
    target_compile_options(inferscale_llama_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    add_test(NAME llama_runtime COMMAND inferscale_llama_tests)
    add_test(NAME llama_cli_help COMMAND inferscale_llama_server --help)

    add_executable(inferscale_llama_http_tests tests/llama_http_test.cpp)
    target_link_libraries(inferscale_llama_http_tests PRIVATE inferscale_http inferscale_llama)
    target_compile_options(inferscale_llama_http_tests PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wshadow)
    add_test(NAME llama_http_slow_reader COMMAND inferscale_llama_http_tests)
    set_tests_properties(llama_http_slow_reader PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 25)
  endif()
endif()
