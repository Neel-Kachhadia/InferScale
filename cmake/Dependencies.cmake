include(FetchContent)

set(INFERSCALE_LLAMA_COMMIT "b29c606e28a01b1bc8c1351026a0fa6e616bf6c4")

function(inferscale_fetch_llama)
  set(GGML_CCACHE OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_TOOLS OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_SERVER OFF CACHE BOOL "" FORCE)
  set(LLAMA_BUILD_COMMON OFF CACHE BOOL "" FORCE)
  FetchContent_Declare(llama_cpp
    GIT_REPOSITORY https://github.com/ggml-org/llama.cpp.git
    GIT_TAG ${INFERSCALE_LLAMA_COMMIT}
    GIT_SHALLOW FALSE)
  FetchContent_MakeAvailable(llama_cpp)
endfunction()
