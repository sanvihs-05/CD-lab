#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLANG_BIN="${CLANG_BIN:-/llvm-build/bin/clang}"
OUT_DIR="${ROOT_DIR}/benchmarks/out"
mkdir -p "${OUT_DIR}"

# The live playground installs the toolchain at warmup; let it skip the re-check.
if [ "${NSSAN_SKIP_INSTALL:-0}" != "1" ]; then
  "${ROOT_DIR}/scripts/install-native-nssan.sh" >/dev/null
fi

measure_average() {
  local output_file
  output_file="$(mktemp)"
  for _ in 1 2 3; do
    /usr/bin/time -f "%e" "$@" >/dev/null 2>>"${output_file}"
  done
  awk '{sum+=$1} END {printf "%.4f", sum/NR}' "${output_file}"
  rm -f "${output_file}"
}

build_pair() {
  local name="$1"
  local source="$2"
  "${CLANG_BIN}" -O0 -g "${source}" -lm -o "${OUT_DIR}/${name}_baseline"
  "${CLANG_BIN}" -O0 -g -fsanitize=numerical "${source}" -lm -o "${OUT_DIR}/${name}_nssan"
}

programs=(
  "LINPACK|linpack_mini"
  "Monte Carlo|monte_carlo_option"
  "Navier-Stokes|navier_stokes_mini"
)

printf '%-16s %-11s %-15s %s\n' "Program" "Baseline" "Instrumented" "Overhead"
printf '%-16s %-11s %-15s %s\n' "-------" "--------" "------------" "--------"

ratio_log_sum=0
count=0

for entry in "${programs[@]}"; do
  IFS='|' read -r label source_stem <<<"${entry}"
  src="${ROOT_DIR}/benchmarks/src/${source_stem}.c"
  build_pair "${source_stem}" "${src}"

  baseline_time="$(measure_average "${OUT_DIR}/${source_stem}_baseline")"
  sanitized_time="$(measure_average env NSSAN_THRESHOLD=1e99 "${OUT_DIR}/${source_stem}_nssan")"
  ratio="$(awk -v base="${baseline_time}" -v san="${sanitized_time}" 'BEGIN { if (base <= 0) printf "%.2f", 1.0; else printf "%.2f", san / base }')"

  printf '%-16s %-11s %-15s %sx\n' "${label}" "${baseline_time}s" "${sanitized_time}s" "${ratio}"

  ratio_log_sum="$(awk -v sum="${ratio_log_sum}" -v ratio="${ratio}" 'BEGIN { printf "%.12f", sum + log(ratio) }')"
  count=$((count + 1))
done

geo_mean="$(awk -v sum="${ratio_log_sum}" -v count="${count}" 'BEGIN { printf "%.2f", exp(sum / count) }')"

printf '\nGeometric mean overhead: %sx\n' "${geo_mean}"
printf 'Herbgrind reference:     ~100x\n'
