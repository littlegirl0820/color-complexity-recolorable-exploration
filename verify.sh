#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 ]]; then
  echo "usage: $0 sources|reduced|full [DRAT_TRIM]" >&2
  exit 2
fi

mode=$1
drat_trim=${2:-${DRAT_TRIM:-drat-trim}}
root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

case $mode in
  sources|reduced|full) ;;
  *)
    echo "unknown verification mode: $mode" >&2
    exit 2
    ;;
esac

check_sources() {
  local missing=0
  local input
  for input in \
    "$root/inputs/residual-family.graph6" \
    "$root/inputs/core.blocks" \
    "$root/inputs/v3-trace.blocks" \
    "$root/inputs/v5-domain4.blocks" \
    "$root/reference/formula.cnf"; do
    if [[ ! -r $input ]]; then
      missing=1
    fi
  done

  if [[ $missing -ne 0 ]]; then
    if [[ -r $root/residual-family.graph6 &&
          -r $root/verify_obstruction_family.mjs &&
          -r $root/verify_initial_tuple_reduction.mjs ]]; then
      node "$root/verify_obstruction_family.mjs" \
        "$root/residual-family.graph6"
      node "$root/verify_initial_tuple_reduction.mjs"
      echo "source-snapshot-checks=VERIFIED"
      echo "lower-bound-certificate=NOT CHECKED (quick checks only; use reproduction/regenerate_and_verify.sh for full verification)"
      return
    fi

    for input in \
      "$root/inputs/residual-family.graph6" \
      "$root/inputs/core.blocks" \
      "$root/inputs/v3-trace.blocks" \
      "$root/inputs/v5-domain4.blocks" \
      "$root/reference/formula.cnf"; do
      if [[ ! -r $input ]]; then
        echo "missing source-check input: $input" >&2
      fi
    done
    exit 2
  fi

  node "$root/checker/verify_obstruction_family.mjs" \
    "$root/inputs/residual-family.graph6"
  node "$root/checker/verify_initial_tuple_reduction.mjs"
  node "$root/checker/verify_cactus_policy_blocks.mjs" \
    --graphs "$root/inputs/residual-family.graph6" \
    --blocks "$root/inputs/core.blocks" \
    --blocks "$root/inputs/v3-trace.blocks" \
    --blocks "$root/inputs/v5-domain4.blocks"

  local temporary
  temporary=$(mktemp -d)
  cleanup_sources() {
    rm -rf -- "$temporary"
  }
  trap cleanup_sources RETURN

  g++ -std=c++20 -O2 -Wall -Wextra -pedantic \
    -DCACTUS_GENERATE_ONLY_STUB \
    -I"$root/generator/generate-only" \
    "$root/generator/exact_3color_family_sat.cpp" \
    -o "$temporary/exact"
  g++ -std=c++20 -O2 -Wall -Wextra -pedantic \
    "$root/generator/widen_cactus_initial_tuple.cpp" \
    -o "$temporary/widen"

  "$temporary/exact" \
    --graphs-file "$root/inputs/residual-family.graph6" \
    --initial-repaints 111 \
    --blocks "$root/inputs/core.blocks" \
    --blocks "$root/inputs/v3-trace.blocks" \
    --blocks "$root/inputs/v5-domain4.blocks" \
    --generate-only \
    --write-cnf "$temporary/branch-111.cnf"
  "$temporary/widen" \
    "$temporary/branch-111.cnf" \
    "$temporary/reference.cnf"
  cmp "$temporary/reference.cnf" "$root/reference/formula.cnf"
  echo "source-to-reference-encoding=VERIFIED"
}

resolve_drat_trim() {
  if [[ $drat_trim == */* ]]; then
    if [[ ! -x $drat_trim ]]; then
      echo "drat-trim is not executable: $drat_trim" >&2
      exit 2
    fi
  else
    local resolved
    resolved=$(command -v "$drat_trim" || true)
    if [[ -z $resolved ]]; then
      echo "drat-trim was not found: $drat_trim" >&2
      exit 2
    fi
    drat_trim=$resolved
  fi
}

check_proof() {
  local label=$1
  local cnf=$2
  local proof=$3
  local temporary raw_log log status
  temporary=$(mktemp -d)
  raw_log=$temporary/check.raw.log
  log=$temporary/check.log
  set +e
  "$drat_trim" "$cnf" "$proof" \
    -t "${DRAT_TRIM_TIMEOUT:-172800}" >"$raw_log" 2>&1
  status=$?
  set -e
  tr -d '\r' <"$raw_log" >"$log"
  if [[ $status -ne 0 ]] ||
     ! grep -Fx 's VERIFIED' "$log" >/dev/null ||
     ! grep -F 'c 0 RAT lemmas in core' "$log" >/dev/null; then
    cat "$log" >&2
    rm -rf -- "$temporary"
    echo "$label proof check failed with exit $status" >&2
    exit 1
  fi
  cat "$log"
  rm -rf -- "$temporary"
  echo "$label=VERIFIED"
}

if [[ -r $root/SHA256SUMS ]]; then
  (cd "$root" && sha256sum -c SHA256SUMS)
fi

case $mode in
  sources)
    check_sources
    ;;
  reduced)
    resolve_drat_trim
    check_proof reduced "$root/reduced/formula.cnf" "$root/reduced/proof.drat"
    ;;
  full)
    check_sources
    resolve_drat_trim
    check_proof reference "$root/reference/formula.cnf" "$root/combined/proof.drat"
    check_proof combined "$root/combined/formula.cnf" "$root/combined/proof.drat"
    check_proof reduced "$root/reduced/formula.cnf" "$root/reduced/proof.drat"
    ;;
esac
