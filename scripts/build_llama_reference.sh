#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
source_dir="${LLAMA_CPP_SOURCE_DIR:-$root/build/llama-source}"
build_dir="$root/build/llama-reference"
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
  -DGGML_CUDA=OFF \
  -DLLAMA_BUILD_COMMON=ON \
  -DLLAMA_BUILD_TESTS=OFF \
  -DLLAMA_BUILD_EXAMPLES=OFF \
  -DLLAMA_BUILD_TOOLS=ON \
  -DLLAMA_BUILD_SERVER=ON \
  -DLLAMA_BUILD_UI=OFF
cmake --build "$build_dir" --target llama-server -j2
"$build_dir/bin/llama-server" --help > "$build_dir/server-help.txt" 2>&1
printf '%s\n' 'CPU reference build: pinned llama-server; no model or performance evidence collected.' > "$build_dir/build-status.txt"
