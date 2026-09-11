#!/usr/bin/env bash
set -euo pipefail

usage() {
  cat >&2 <<'EOF'
usage: regenerate_and_verify.sh OUTPUT_DIRECTORY [OPTIONS]

Options:
  --jobs N             Run N branches concurrently (default: $JOBS or 1).
  --cadical PATH       CaDiCaL executable (default: $CADICAL or cadical).
  --drat-trim PATH     drat-trim executable (default: $DRAT_TRIM or drat-trim).
  --cnf-only           Generate and hash-check the four CNFs without solving.
  -h, --help           Show this help.

Environment:
  JOBS                  Same as --jobs.
  CADICAL               Same as --cadical.
  DRAT_TRIM             Same as --drat-trim.
  DRAT_TRIM_TIMEOUT     Per-proof timeout in seconds (default: 172800).
EOF
}

if [[ ${1:-} == -h || ${1:-} == --help ]]; then
  usage
  exit 0
fi
if [[ $# -lt 1 ]]; then
  usage
  exit 2
fi

output=$1
shift
jobs=${JOBS:-1}
cadical=${CADICAL:-cadical}
drat_trim=${DRAT_TRIM:-drat-trim}
drat_timeout=${DRAT_TRIM_TIMEOUT:-172800}
cnf_only=0

while [[ $# -gt 0 ]]; do
  case $1 in
    --jobs)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      jobs=$2
      shift 2
      ;;
    --cadical)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      cadical=$2
      shift 2
      ;;
    --drat-trim)
      [[ $# -ge 2 ]] || { usage; exit 2; }
      drat_trim=$2
      shift 2
      ;;
    --cnf-only)
      cnf_only=1
      shift
      ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "unknown option: $1" >&2
      usage
      exit 2
      ;;
  esac
done

if [[ ! $jobs =~ ^[1-4]$ ]]; then
  echo "--jobs must be an integer from 1 to 4" >&2
  exit 2
fi
if [[ ! $drat_timeout =~ ^[1-9][0-9]*$ ]]; then
  echo "DRAT_TRIM_TIMEOUT must be a positive integer" >&2
  exit 2
fi
if [[ -e $output ]]; then
  echo "refusing to overwrite output: $output" >&2
  exit 2
fi

root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
code_root=$(cd "$root/.." && pwd)
inputs=$root/inputs

bash "$code_root/prepare_reproduction_inputs.sh"

for command in node g++ sha256sum awk grep tr tee find sort xargs date; do
  if ! command -v "$command" >/dev/null 2>&1; then
    echo "required command was not found: $command" >&2
    exit 2
  fi
done

resolve_executable() {
  local value=$1
  if [[ $value == */* ]]; then
    [[ -x $value ]] || return 1
    printf '%s\n' "$value"
  else
    command -v "$value"
  fi
}

if [[ $cnf_only -eq 0 ]]; then
  requested_cadical=$cadical
  requested_drat_trim=$drat_trim
  if ! cadical=$(resolve_executable "$requested_cadical"); then
    echo "CaDiCaL was not found or is not executable: $requested_cadical" >&2
    exit 2
  fi
  if ! drat_trim=$(resolve_executable "$requested_drat_trim"); then
    echo "drat-trim was not found or is not executable: $requested_drat_trim" >&2
    exit 2
  fi
fi

for input in \
  "$root/INPUT_SHA256SUMS" \
  "$root/EXPECTED_CNF_SHA256SUMS" \
  "$inputs/core.blocks" \
  "$inputs/residual-family.graph6" \
  "$inputs/v3-trace.blocks" \
  "$inputs/v5-domain4.blocks" \
  "$code_root/generator/exact_3color_family_sat.cpp" \
  "$code_root/generator/generate-only/cadical.hpp" \
  "$code_root/checker/verify_cactus_policy_blocks.mjs" \
  "$code_root/verify_obstruction_family.mjs" \
  "$code_root/verify_initial_tuple_reduction.mjs"; do
  if [[ ! -r $input ]]; then
    echo "required input is missing: $input" >&2
    exit 2
  fi
done

(cd "$root" && sha256sum -c INPUT_SHA256SUMS)

mkdir -p "$output/branches" "$output/logs" "$output/tools"
output=$(cd "$output" && pwd)

node "$code_root/verify_obstruction_family.mjs" \
  "$inputs/residual-family.graph6" | tee "$output/logs/obstruction-family.log"
node "$code_root/verify_initial_tuple_reduction.mjs" \
  | tee "$output/logs/initial-tuple-reduction.log"
node "$code_root/checker/verify_cactus_policy_blocks.mjs" \
  --graphs "$inputs/residual-family.graph6" \
  --blocks "$inputs/core.blocks" \
  --blocks "$inputs/v3-trace.blocks" \
  --blocks "$inputs/v5-domain4.blocks" \
  | tee "$output/logs/policy-blocks.log"

exact=$output/tools/exact_3color_family_sat
g++ -std=c++20 -O2 -Wall -Wextra -pedantic \
  -DCACTUS_GENERATE_ONLY_STUB \
  -I"$code_root/generator/generate-only" \
  "$code_root/generator/exact_3color_family_sat.cpp" \
  -o "$exact"

sha256sum \
  "$code_root/generator/exact_3color_family_sat.cpp" \
  "$code_root/generator/generate-only/cadical.hpp" \
  "$code_root/checker/verify_cactus_policy_blocks.mjs" \
  "$code_root/verify_obstruction_family.mjs" \
  "$code_root/verify_initial_tuple_reduction.mjs" \
  "$exact" >"$output/SOURCE_SHA256SUMS"

run_branch() {
  local tuple=$1
  local branch=$output/branches/$tuple
  local expected actual solve_status check_status started finished raw_log
  mkdir "$branch"
  started=$(date -u +%Y-%m-%dT%H:%M:%SZ)
  printf 'tuple=%s\nstate=generating\nstarted=%s\n' \
    "$tuple" "$started" >"$branch/status.txt"

  "$exact" \
    --graphs-file "$inputs/residual-family.graph6" \
    --initial-repaints "$tuple" \
    --blocks "$inputs/core.blocks" \
    --blocks "$inputs/v3-trace.blocks" \
    --blocks "$inputs/v5-domain4.blocks" \
    --generate-only \
    --write-cnf "$branch/formula.cnf" \
    >"$branch/generate.log" 2>&1

  actual=$(sha256sum "$branch/formula.cnf" | awk '{print $1}')
  expected=$(awk -v name="$tuple.cnf" '$2 == name {print $1}' \
    "$root/EXPECTED_CNF_SHA256SUMS")
  if [[ -z $expected || $actual != "$expected" ]]; then
    printf 'tuple=%s\nstate=cnf-hash-mismatch\nexpected=%s\nactual=%s\n' \
      "$tuple" "$expected" "$actual" >"$branch/status.txt"
    echo "CNF hash mismatch for tuple $tuple" >&2
    return 1
  fi
  echo "$actual  formula.cnf" >"$branch/SHA256SUMS"

  if [[ $cnf_only -eq 1 ]]; then
    finished=$(date -u +%Y-%m-%dT%H:%M:%SZ)
    printf 'tuple=%s\nstate=cnf-verified\nstarted=%s\nfinished=%s\n' \
      "$tuple" "$started" "$finished" >"$branch/status.txt"
    echo "branch=$tuple cnf=VERIFIED"
    return
  fi

  printf 'tuple=%s\nstate=solving\nstarted=%s\n' \
    "$tuple" "$started" >"$branch/status.txt"
  set +e
  "$cadical" "$branch/formula.cnf" "$branch/proof.drat" \
    >"$branch/solve.log" 2>&1
  solve_status=$?
  set -e
  if [[ $solve_status -ne 20 ]] || \
     ! grep -Fx 's UNSATISFIABLE' "$branch/solve.log" >/dev/null; then
    printf 'tuple=%s\nstate=solve-failed\nexit=%s\n' \
      "$tuple" "$solve_status" >"$branch/status.txt"
    echo "CaDiCaL did not prove tuple $tuple UNSAT (exit=$solve_status)" >&2
    return 1
  fi

  raw_log=$branch/check.raw.log
  set +e
  "$drat_trim" "$branch/formula.cnf" "$branch/proof.drat" \
    -t "$drat_timeout" >"$raw_log" 2>&1
  check_status=$?
  set -e
  tr -d '\r' <"$raw_log" >"$branch/check.log"
  if [[ $check_status -ne 0 ]] || \
     ! grep -Fx 's VERIFIED' "$branch/check.log" >/dev/null || \
     ! grep -F 'c 0 RAT lemmas in core' "$branch/check.log" >/dev/null; then
    printf 'tuple=%s\nstate=proof-check-failed\nexit=%s\n' \
      "$tuple" "$check_status" >"$branch/status.txt"
    echo "DRAT verification failed for tuple $tuple (exit=$check_status)" >&2
    return 1
  fi

  (
    cd "$branch"
    sha256sum formula.cnf proof.drat >SHA256SUMS
  )
  finished=$(date -u +%Y-%m-%dT%H:%M:%SZ)
  printf 'tuple=%s\nstate=verified\nstarted=%s\nfinished=%s\n' \
    "$tuple" "$started" "$finished" >"$branch/status.txt"
  echo "branch=$tuple cnf=VERIFIED proof=VERIFIED"
}

tuples=(111 121 211 221)
for ((offset = 0; offset < ${#tuples[@]}; offset += jobs)); do
  pids=()
  for ((index = offset;
        index < offset + jobs && index < ${#tuples[@]};
        ++index)); do
    run_branch "${tuples[index]}" &
    pids+=("$!")
  done
  failed=0
  for pid in "${pids[@]}"; do
    if ! wait "$pid"; then
      failed=1
    fi
  done
  if [[ $failed -ne 0 ]]; then
    echo "one or more branches failed" >&2
    exit 1
  fi
done

(
  cd "$output"
  find branches logs -type f -print0 | sort -z | xargs -0 sha256sum \
    >SHA256SUMS
)

if [[ $cnf_only -eq 1 ]]; then
  echo "four-branch-cnf-regeneration=VERIFIED output=$output"
else
  echo "four-branch-lower-bound=VERIFIED output=$output"
fi
