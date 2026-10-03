#!/usr/bin/env bash
set -euo pipefail

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
model_path="${MODEL_PATH:-/home/neel/models/inferscale/tinyllama/TinyLlama-1.1B-Chat-v1.0-Q4_K_M.gguf}"
expected_model_sha="9fecc3b3cd76bba89d504f29b616eedf7da85b96540e490ca5824d3f7d2776a0"
expected_backend_commit="b29c606e28a01b1bc8c1351026a0fa6e616bf6c4"
repo_commit="$(git -C "$repo_root" rev-parse --short=12 HEAD)"
build_dir="$(mktemp -d "${TMPDIR:-/tmp}/inferscale-release-${repo_commit}.XXXXXX")"
evidence_root="${RELEASE_EVIDENCE_DIR:-$repo_root/artifacts/release-v1/reproduction}"
server_bin="$build_dir/cuda/inferscale_llama_server"

for tool in cmake ninja c++ nvcc nvidia-smi python3 sha256sum git; do
  command -v "$tool" >/dev/null || { echo "missing required tool: $tool" >&2; exit 2; }
done
python3 -c 'import httpx, yaml' || {
  echo "install the pinned Python dependencies from requirements-bench.txt" >&2
  exit 2
}
test -s "$model_path" || { echo "missing model: $model_path" >&2; exit 2; }
actual_model_sha="$(sha256sum "$model_path" | awk '{print $1}')"
[[ "$actual_model_sha" == "$expected_model_sha" ]] || {
  echo "model SHA-256 mismatch: $actual_model_sha" >&2
  exit 2
}
actual_backend_commit="$(sed -n 's/.*INFERSCALE_LLAMA_COMMIT "\([0-9a-f]*\)".*/\1/p' "$repo_root/cmake/Dependencies.cmake")"
[[ "$actual_backend_commit" == "$expected_backend_commit" ]] || {
  echo "pinned llama.cpp commit mismatch: $actual_backend_commit" >&2
  exit 2
}
nvidia-smi -L
nvidia-smi --query-gpu=name,memory.total,driver_version --format=csv

cmake -S "$repo_root" -B "$build_dir/cuda" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=ON \
  -DINFERSCALE_WITH_LLAMA=ON -DGGML_CUDA=ON -DINFERSCALE_ENABLE_NVTX=ON
cmake --build "$build_dir/cuda" --parallel "${BUILD_JOBS:-4}"
INFERSCALE_TEST_MODEL="$model_path" INFERSCALE_TEST_GPU_LAYERS=99 \
  ctest --test-dir "$build_dir/cuda" --output-on-failure
"$server_bin" --help

cmake -S "$repo_root" -B "$build_dir/asan-ubsan" -G Ninja \
  -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON -DINFERSCALE_ENABLE_SANITIZERS=ON
cmake --build "$build_dir/asan-ubsan" --parallel "${BUILD_JOBS:-4}"
ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 \
  ctest --test-dir "$build_dir/asan-ubsan" --output-on-failure

python3 -m unittest discover -s "$repo_root/tests" -p 'test_*.py' -v
python3 "$repo_root/bench/stats.py"
python3 -m py_compile "$repo_root/bench/run.py" "$repo_root/bench/stats.py" \
  "$repo_root/bench/release_analysis.py" "$repo_root/scripts/run_release_campaign.py"
python3 -m json.tool "$repo_root/bench/schema/request-record.schema.json" >/dev/null
python3 - "$repo_root/bench/workloads" <<'PY'
from pathlib import Path
import sys
import yaml
for path in sorted(Path(sys.argv[1]).glob('*.yaml')):
    workload = yaml.safe_load(path.read_text())
    assert int(workload['requests']) > 0, path
    for request in workload['classes']:
        output = request['output_tokens']
        assert max(output) <= 128 if isinstance(output, list) else output <= 128, path
PY

python3 "$repo_root/scripts/run_release_campaign.py" \
  --server "$server_bin" --model "$model_path" \
  --workload "$repo_root/bench/workloads/phase5_interactive.yaml" \
  --output "$evidence_root" --treatments fifo --requests 8 --trials 1 \
  --rate 4 --seed 73103
latest_campaign="$(find "$evidence_root" -mindepth 1 -maxdepth 2 -type f -name run-index.jsonl -printf '%T@ %h\n' | sort -nr | head -1 | cut -d' ' -f2-)"
test -s "$latest_campaign/run-index.jsonl"
python3 "$repo_root/bench/release_analysis.py" \
  --index "$latest_campaign/run-index.jsonl" --output "$evidence_root/analysis"

printf 'clean reproduction passed\ncommit=%s\nmodel_sha256=%s\nbackend_commit=%s\nbuild_dir=%s\nsmoke_campaign=%s\n' \
  "$repo_commit" "$actual_model_sha" "$actual_backend_commit" "$build_dir" "$latest_campaign"
