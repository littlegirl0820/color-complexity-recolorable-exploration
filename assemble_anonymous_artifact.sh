#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 2 ]]; then
  echo "usage: $0 SOURCE_CERTIFICATE_BUNDLE OUTPUT_DIRECTORY" >&2
  exit 2
fi

source_bundle=$1
output=$2
root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)

if [[ ! -d $source_bundle ]]; then
  echo "source certificate bundle is missing: $source_bundle" >&2
  exit 2
fi
if [[ -e $output ]]; then
  echo "refusing to overwrite output: $output" >&2
  exit 2
fi
source_bundle=$(cd "$source_bundle" && pwd)

for input in \
  "$source_bundle/SHA256SUMS" \
  "$source_bundle/SOURCE_SHA256SUMS" \
  "$source_bundle/inputs/core.blocks" \
  "$source_bundle/inputs/v3-trace.blocks" \
  "$source_bundle/inputs/v5-domain4.blocks" \
  "$source_bundle/semantic/formula.cnf" \
  "$source_bundle/combined/formula.cnf" \
  "$source_bundle/combined/proof.drat" \
  "$source_bundle/fast/formula.cnf" \
  "$source_bundle/fast/proof.drat" \
  "$source_bundle/fast/variable-map.tsv"; do
  if [[ ! -r $input ]]; then
    echo "required source file is missing: $input" >&2
    exit 2
  fi
done

(cd "$source_bundle" && sha256sum -c SHA256SUMS && sha256sum -c SOURCE_SHA256SUMS)

mkdir -p \
  "$output/inputs" \
  "$output/generator" \
  "$output/generator/generate-only" \
  "$output/checker" \
  "$output/reference" \
  "$output/combined" \
  "$output/reduced"

install -m 0644 "$root/ARTIFACT_README.md" "$output/README.md"
install -m 0755 "$root/verify.sh" "$output/verify.sh"
install -m 0755 "$root/sanity_positive_control.sh" "$output/sanity_positive_control.sh"
install -m 0755 "$root/checker/simulate_algorithms.py" "$output/checker/simulate_algorithms.py"
install -m 0644 "$root/residual-family.graph6" "$output/inputs/residual-family.graph6"
install -m 0644 \
  "$root/generator/exact_3color_family_sat.cpp" \
  "$root/generator/widen_cactus_initial_tuple.cpp" \
  "$output/generator/"
install -m 0644 \
  "$root/generator/generate-only/cadical.hpp" \
  "$output/generator/generate-only/"
install -m 0644 \
  "$root/checker/verify_cactus_policy_blocks.ts" \
  "$root/checker/verify_cactus_policy_blocks.mjs" \
  "$root/checker/normalize_policy_graph6.mjs" \
  "$root/verify_obstruction_family.mjs" \
  "$root/verify_initial_tuple_reduction.mjs" \
  "$output/checker/"

node "$root/checker/normalize_policy_graph6.mjs" \
  "$source_bundle/inputs/core.blocks" "$output/inputs/core.blocks"
node "$root/checker/normalize_policy_graph6.mjs" \
  "$source_bundle/inputs/v3-trace.blocks" "$output/inputs/v3-trace.blocks"
node "$root/checker/normalize_policy_graph6.mjs" \
  "$source_bundle/inputs/v5-domain4.blocks" "$output/inputs/v5-domain4.blocks"

cp --reflink=auto "$source_bundle/semantic/formula.cnf" "$output/reference/formula.cnf"
cp --reflink=auto "$source_bundle/combined/formula.cnf" "$output/combined/formula.cnf"
cp --reflink=auto "$source_bundle/combined/proof.drat" "$output/combined/proof.drat"
cp --reflink=auto "$source_bundle/fast/formula.cnf" "$output/reduced/formula.cnf"
cp --reflink=auto "$source_bundle/fast/proof.drat" "$output/reduced/proof.drat"
cp --reflink=auto "$source_bundle/fast/variable-map.tsv" "$output/reduced/variable-map.tsv"

(
  cd "$output"
  find . -type f ! -name SHA256SUMS -print0 |
    sort -z |
    xargs -0 sha256sum >SHA256SUMS
)

"$output/verify.sh" sources
echo "anonymous-artifact=ASSEMBLED path=$output"
