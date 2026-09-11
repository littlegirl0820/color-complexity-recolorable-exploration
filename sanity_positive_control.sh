#!/usr/bin/env bash
# Positive and negative controls for the exact CNF generator.
# The encoding of the tree-and-cycle subfamily of H (including merge clauses) must be satisfiable, must stay
# satisfiable when the rule of Algorithm 1 (three-color trees and cycles) is fixed, and must
# become unsatisfiable when one rule of Algorithm 1 is broken.
set -euo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
cadical=${CADICAL:-cadical}
work=$(mktemp -d)
trap 'rm -rf -- "$work"' EXIT
g++ -std=c++20 -O2 -DCACTUS_GENERATE_ONLY_STUB -I"$root/generator/generate-only" \
  "$root/generator/exact_3color_family_sat.cpp" -o "$work/exact"
printf 'Bw\nCs\nC]\nDk_\nDLo\n' >"$work/trees-cycles.g6"   # C3, K_{1,3}, C4, T5, C5
policy=$(python3 - <<'PY'
# Observation order of the generator: current color 0..2, degree 1..3, c0 in 0..deg, c1 in 0..deg-c0.
# Colors init=0, l0=1, l1=2; action = repaint*3 + target, stop = 9.  Rules D1-D11 of Algorithm 1.
def tc3(cur, c0, c1, c2):
    if cur == 0:
        if c0 >= 1 and c1 == 0 and c2 == 0: return 3   # D2 (l0, init)
        if c0 >= 1 and c1 >= 1: return 6                # D3 (l1, init)
        if c0 >= 1 and c2 >= 1: return 2                # D4 (init, l1)
        if c2 >= 1: return 8                            # D5 (l1, l1)
        if c1 >= 1: return 7                            # D6 (l1, l0)
    if cur == 1:
        if c0 >= 1 and c2 >= 1: return 3                # D7 (l0, init)
        if c1 >= 1 and c2 >= 1: return 7                # D8 (l1, l0)
        return 9                                        # D9 stop, or unreachable
    if cur == 2:
        if c0 >= 1 and c1 >= 1: return 3                # D10 (l0, init)
        if c1 >= 1 and c2 >= 1: return 7                # D11 (l1, l0)
    return 9
acts = [tc3(cur, c0, c1, deg - c0 - c1) for cur in range(3) for deg in range(1, 4)
        for c0 in range(deg + 1) for c1 in range(deg - c0 + 1)]
print(",".join(map(str, acts)))
PY
)
broken=$(python3 - "$policy" <<'PY'
import sys

observations = [(cur, c0, c1, deg - c0 - c1)
                for cur in range(3) for deg in range(1, 4)
                for c0 in range(deg + 1) for c1 in range(deg - c0 + 1)]
actions = sys.argv[1].split(',')
assert len(actions) == len(observations)
# Current color 0 and a single neighbor of color 0: generator index 2.
index = observations.index((0, 1, 0, 0))
assert index == 2 and actions[index] == '3'  # D2: repaint 1, move to 0.
actions[index] = '9'                       # Stop immediately at a leaf start.
print(','.join(actions))
PY
)
run() { # name expected [extra generator args...]
  local name=$1 expected=$2; shift 2
  "$work/exact" --graphs-file "$work/trees-cycles.g6" "$@" --generate-only --write-cnf "$work/$name.cnf" >/dev/null
  local status=0 result
  "$cadical" -q "$work/$name.cnf" >"$work/$name.out" || status=$?
  case $status in
    10) result=SATISFIABLE ;;
    20) result=UNSATISFIABLE ;;
    *) echo "solver failed on $name with status $status" >&2; exit 1 ;;
  esac
  echo "$name=$result"
  [[ $result == "$expected" ]] || { echo "unexpected result for $name" >&2; exit 1; }
}
run trees-cycles-free SATISFIABLE
run trees-cycles-algorithm1 SATISFIABLE --fixed-policy "$policy"
run trees-cycles-broken-rule UNSATISFIABLE --fixed-policy "$broken"
echo "generator-positive-control=VERIFIED"
