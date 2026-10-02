#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
model="${MODEL_PATH:-}"
if [[ -z "$model" || ! -f "$model" ]]; then
  echo "set MODEL_PATH to an existing GGUF model" >&2
  exit 2
fi
server="$root/build/llama-reference-cuda/bin/llama-server"
if [[ ! -x "$server" ]]; then
  echo "build the pinned reference first with scripts/build_llama_reference.sh" >&2
  exit 2
fi

gpu_layers="${GPU_LAYERS:-99}"
ctx_size="${CTX_SIZE:-32768}"
parallel="${PARALLEL:-16}"
batch_size="${BATCH_SIZE:-2048}"
ubatch_size="${UBATCH_SIZE:-512}"
port="${PORT:-8080}"
command=("$server" --model "$model" --alias local-model --host 127.0.0.1 --port "$port"
  --ctx-size "$ctx_size" --parallel "$parallel" --batch-size "$batch_size" --ubatch-size "$ubatch_size"
  --cont-batching --no-kv-unified --flash-attn off --n-gpu-layers "$gpu_layers"
  --threads 8 --threads-batch 8 --threads-http 4
  --cache-type-k f16 --cache-type-v f16
  --seed 1 --temp 0 --top-k 1 --top-p 1 --no-webui --metrics)
printf '%q ' "${command[@]}" > "$root/build/reference-command.txt"
printf '\n' >> "$root/build/reference-command.txt"
{
  printf 'model: %s\n' "$model"
  sha256sum "$model"
  printf 'gpu_layers: %s\ntotal_context: %s\nparallel: %s\ncontext_per_sequence: %s\nbatch: %s\nubatch: %s\nkv_unified: false\ncontinuous_batching: true\nflash_attention: off\nport: %s\nsampling: seed=1,temp=0,top_k=1,top_p=1 (greedy)\n' \
    "$gpu_layers" "$ctx_size" "$parallel" "$((ctx_size / parallel))" "$batch_size" "$ubatch_size" "$port"
} > "$root/build/reference-run-metadata.txt"
exec "${command[@]}"
