#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
source_dir="${LLAMA_CPP_SOURCE_DIR:-$root/build/llama-source}"
build_dir="$root/build/llama-reference-cuda"
expected="b29c606e28a01b1bc8c1351026a0fa6e616bf6c4"

if [[ ! -f "$source_dir/CMakeLists.txt" ]]; then
  echo "pinned llama.cpp checkout not found: $source_dir" >&2
  exit 2
fi
actual="$(git -C "$source_dir" rev-parse HEAD)"
if [[ "$actual" != "$expected" ]]; then
  echo "llama.cpp pin mismatch: expected $expected, got $actual" >&2
  exit 2
fi

cmake -S "$source_dir" -B "$build_dir" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DGGML_CCACHE=OFF \
  -DGGML_CUDA=ON \
  -DLLAMA_BUILD_COMMON=ON \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_TOOLS=ON \
  -DLLAMA_BUILD_SERVER=ON \
  -DLLAMA_BUILD_UI=OFF 2>&1 | tee "$build_dir/configure.log"
cmake --build "$build_dir" --target llama-server -j2 2>&1 | tee "$build_dir/build.log"
"$build_dir/bin/llama-server" --help > "$build_dir/server-help.txt" 2>&1
{
  printf 'llama.cpp commit: %s\n' "$actual"
  printf 'configure: cmake -S %q -B %q -G Ninja -DCMAKE_BUILD_TYPE=Release -DGGML_CCACHE=OFF -DGGML_CUDA=ON -DLLAMA_BUILD_COMMON=ON -DLLAMA_BUILD_TESTS=OFF -DLLAMA_BUILD_EXAMPLES=OFF -DLLAMA_BUILD_TOOLS=ON -DLLAMA_BUILD_SERVER=ON -DLLAMA_BUILD_UI=OFF\n' "$source_dir" "$build_dir"
  rg 'Using CMAKE_CUDA_ARCHITECTURES=' "$build_dir/configure.log"
  nvcc --version
  printf 'ninja: %s\n' "$(ninja --version)"
  grep -E '^(CMAKE_BUILD_TYPE|CMAKE_CUDA_ARCHITECTURES|CMAKE_CXX_COMPILER|CMAKE_CUDA_COMPILER|GGML_CUDA|LLAMA_BUILD_COMMON|LLAMA_BUILD_SERVER|LLAMA_BUILD_TOOLS):' "$build_dir/CMakeCache.txt"
  printf 'nvidia-smi at build time:\n'
  nvidia-smi
} > "$build_dir/build-status.txt"
