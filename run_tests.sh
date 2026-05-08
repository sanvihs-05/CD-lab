#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CLANG_BIN="${CLANG_BIN:-/llvm-build/bin/clang}"
OUT_DIR="${ROOT_DIR}/tests/out/native"

green="$(printf '\033[1;32m')"
red="$(printf '\033[1;31m')"
yellow="$(printf '\033[1;33m')"
gray="$(printf '\033[1;30m')"
reset="$(printf '\033[0m')"

"${ROOT_DIR}/scripts/install-native-nssan.sh"
mkdir -p "${OUT_DIR}"

cases=(
  "t01_cancellation|quadratic_near_double_root|report|1e-5"
  "t02_naive_sum|naive_sum_1e7|report|1e-5"
  "t03_kahan|kahan_sum_1e7|clean|1e-5"
  "t04_dot_product|dot_product_float|report|1e-5"
  "t05_inf_propagation|exp_series_overflow|report|1e-5"
  "t06_nan_sqrt|sqrt_negative_eps|report|1e-5"
  "t07_horner_vs_naive|horner_vs_naive_poly|report|1e-5"
  "t08_ill_conditioned|matrix_ill_cond|report|1e-5"
  "t09_euler_stiff|stokes_euler|report|1e-5"
  "t10_clean_baseline|no_cancel_baseline|clean|1e-5"
  "t11_fma_rounding|fnmadd_fused|report|1e-8"
  "t12_one_minus_cos|catastrophic_1minus_cos|report|1e-5"
)

passed=0
failed=0
true_negatives=0
false_positives=0

for entry in "${cases[@]}"; do
  IFS='|' read -r case_id source_name expectation threshold <<<"${entry}"
  src="${ROOT_DIR}/tests/smoke/${source_name}.c"
  bin="${OUT_DIR}/${case_id}"
  compile_log="${OUT_DIR}/${case_id}.compile.txt"
  stdout_file="${OUT_DIR}/${case_id}.stdout.txt"
  stderr_file="${OUT_DIR}/${case_id}.stderr.txt"

  if ! "${CLANG_BIN}" -g -O0 -fsanitize=numerical "${src}" -lm -o "${bin}" \
      >"${compile_log}" 2>&1; then
    compile_summary="$(tail -n 1 "${compile_log}" | tr -d '\r')"
    if [[ -z "${compile_summary}" ]]; then
      compile_summary="see ${compile_log}"
    fi
    printf '%s[FAIL]%s %-22s - compile failed: %s\n' \
      "${red}" "${reset}" "${case_id}" "${compile_summary}"
    failed=$((failed + 1))
    continue
  fi

  NSSAN_THRESHOLD="${threshold}" "${bin}" >"${stdout_file}" 2>"${stderr_file}" || true

  if grep -q "NUMERICAL SANITIZER: ERROR" "${stderr_file}"; then
    reported=1
    type_summary="$(grep -m1 '^  Type:' "${stderr_file}" | sed 's/^  Type:     //')"
    error_summary="$(grep -m1 '^  error:' "${stderr_file}" | sed 's/^  error:    //')"
  else
    reported=0
    type_summary=""
    error_summary=""
  fi

  if [[ "${expectation}" == "report" && "${reported}" -eq 1 ]]; then
    passed=$((passed + 1))
    detail="${type_summary}"
    if [[ -n "${error_summary}" && "${error_summary}" != "non-finite value encountered" ]]; then
      detail="${detail} (${error_summary})"
    fi
    printf '%s[PASS]%s %-22s - %s\n' "${green}" "${reset}" "${case_id}" "${detail}"
  elif [[ "${expectation}" == "clean" && "${reported}" -eq 0 ]]; then
    passed=$((passed + 1))
    true_negatives=$((true_negatives + 1))
    printf '%s[PASS]%s %-22s - clean, no report\n' "${gray}" "${reset}" "${case_id}"
  else
    failed=$((failed + 1))
    if [[ "${expectation}" == "clean" ]]; then
      false_positives=$((false_positives + 1))
      printf '%s[FAIL]%s %-22s - unexpected report\n' "${red}" "${reset}" "${case_id}"
    else
      printf '%s[FAIL]%s %-22s - expected a report but saw none\n' \
        "${red}" "${reset}" "${case_id}"
    fi
  fi
done

printf '\n'
if [[ "${failed}" -eq 0 ]]; then
  printf '%sResults: %d/%d passed | %d true negatives | %d false positives%s\n' \
    "${green}" "${passed}" "${#cases[@]}" "${true_negatives}" "${false_positives}" "${reset}"
else
  printf '%sResults: %d/%d passed | %d failed | %d false positives%s\n' \
    "${yellow}" "${passed}" "${#cases[@]}" "${failed}" "${false_positives}" "${reset}"
  exit 1
fi
