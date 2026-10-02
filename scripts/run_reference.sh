#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
model="${MODEL_PATH:-}"
if [[ -z "$model" || ! -f "$model" ]]; then
  echo "set MODEL_PATH to an existing GGUF model" >&2
  exit 2
fi
server="$root/build/llama-reference/bin/llama-server"
if [[ ! -x "$server" ]]; then
  echo "build the pinned reference first with scripts/build_llama_reference.sh" >&2
  exit 2
fi

command=("$server" --model "$model" --host 127.0.0.1 --port 8080
  --ctx-size 4096 --parallel 4 --batch-size 2048 --ubatch-size 512
  --cont-batching --flash-attn off --n-gpu-layers 0
  --threads 8 --threads-batch 8 --threads-http 4
  --cache-type-k f16 --cache-type-v f16
  --seed 1 --temp 0 --top-k 1 --top-p 1 --no-webui)
printf '%q ' "${command[@]}" > "$root/build/reference-command.txt"
printf '\n' >> "$root/build/reference-command.txt"
exec "${command[@]}"
