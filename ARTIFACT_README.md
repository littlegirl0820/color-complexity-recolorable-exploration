# Verification artifact layout

This document describes the optional precomputed-proof layout produced by
`assemble_anonymous_artifact.sh`.  It applies to a directory with `inputs/`,
`reference/`, `combined/`, and `reduced/`.  When this document is read in the
source snapshot, those large proof files are not included; follow
`reproduction/README.md` to regenerate and verify the four branches instead.

This artifact supports the three-color lower bound for exploration of
subcubic pseudotrees, and hence the four-color lower bound for cacti and for
graphs whose blocks are simple cycles or complete bipartite graphs.
It contains no author-identifying files and does not include the SAT solver
used to discover the proof.

The trusted path has two independent parts.  First, deterministic checkers
validate the nine standard-graph6 obstruction graphs, the reduction of 27 initial
tuples to four representatives, every imported policy clause, and regeneration
of the exact reference encoding.  Second, `drat-trim` checks that the reference encoding,
its combined contradiction core, and the reduced encoding are unsatisfiable.

## Requirements

- Bash, Node.js, and a C++20 compiler;
- `drat-trim` for proof checking;
- about 3 GiB of free memory for the largest proof check.

The reference run used Ubuntu 24.04 (x86-64), Bash 5.2.21, Node.js 22.19.0,
G++ 13.3.0, and `drat-trim` commit
`2e3b2dc0ecf938addbd779d42877b6ed69d9a985`.  The proof checks took
approximately 164 minutes for the reference encoding, 132 minutes for the
combined core, and 79 minutes for the reduced encoding, with about 3 GiB peak
memory for the largest check.  Times depend on the machine.

## Verification levels

| Mode | What it checks | What it does not establish by itself |
| --- | --- | --- |
| `sources` | obstruction graphs, initial-tuple reduction, every imported trace/domain policy clause, and byte-identical regeneration of the reference CNF | CNF unsatisfiability |
| `reduced` | the reduced CNF/DRAT pair | correspondence between the reduced variables and the exploration model |
| `full` | all source checks plus DRAT checks of the reference, combined, and reduced objects | proof discovery and proof generation are not rerun; neither is part of the trusted base |

Thus `sources` checks the semantic translation described in Appendix A, while
the DRAT modes check propositional unsatisfiability.  The theorem uses both
layers; the combined and reduced objects provide additional proof paths for
the reference encoding.

## Commands

The source and encoding checks take about two minutes in the reference
environment and do not require a SAT or DRAT solver:

```sh
bash ./verify.sh sources
```

The smaller propositional proof can be checked separately:

```sh
bash ./verify.sh reduced /path/to/drat-trim
```

The full path reruns all source checks and all three DRAT checks:

```sh
bash ./verify.sh full /path/to/drat-trim
```

Every command first checks `SHA256SUMS`.  Successful source verification ends
with `source-to-reference-encoding=VERIFIED`; successful proof checks end with the
corresponding `reference=VERIFIED`, `combined=VERIFIED`, or
`reduced=VERIFIED` marker.

The wrapper invokes binary-DRAT checking in the following form (and analogously
for the other two objects):

```sh
/path/to/drat-trim reduced/formula.cnf reduced/proof.drat -t 172800
```

Set `DRAT_TRIM_TIMEOUT` to change the timeout in seconds.

## Additional checks

`checker/simulate_algorithms.py {tc3|c4|us5} N` exhaustively simulates the three
exploration algorithms against every adversarial choice from every start on all
labeled graphs of their classes with at most `N` vertices.  `sanity_positive_control.sh`
checks that the generator's encoding of the tree-and-cycle subfamily, including merge clauses, is
satisfiable, stays satisfiable with the three-color rule fixed, and becomes
unsatisfiable when the degree-one all-zero action is replaced by a stop
(requires `cadical`).

## Layout

- `inputs/`: the fixed standard-graph6 family and checked policy-clause inputs;
- `generator/`: the exact finite-state CNF generator and tuple-widening tool;
- `checker/`: independent obstruction-family, tuple-reduction, and policy-clause checkers;
- `reference/`: the full reference encoding;
- `combined/`: the combined contradiction core and its binary DRAT proof;
- `reduced/`: the densely renumbered reduced encoding, binary DRAT proof, and variable map.

The obstruction order is fixed because it determines variable numbering.  The
checker asserts connectedness, the pseudotree and cactus properties, maximum
degree at most three, pairwise nonisomorphism, and equality with the nine
shapes drawn in the paper.

The header in `generator/generate-only/` is a deliberately inert CaDiCaL API
stub used only by `verify.sh sources`: the exact generator emits clauses both
to its DIMACS stream and to an incremental solver, but deterministic generation
does not call the solver.  The compilation macro prevents accidental use of
the stub in a solving build.

## graph6 convention and proof continuity

An earlier development decoder enumerated graph6 payload bits in row-major
edge order.  Its strings were therefore not standard graph6, although that
decoder produced exactly the intended labeled edge sets.  This artifact stores
the same labeled graphs in standard graph6 and reads them in standard graph6
order.  Graph-name comments in the policy inputs were converted
label-preservingly; the clauses were not changed.

Regenerating the 111-branch CNF from these standard inputs gives SHA-256
`61568f07f895ac1bfb7be2f0019890e316aaf08340475fbdada4e434ee33de2d`,
identical to the branch from which `reference/formula.cnf` was produced.
`verify.sh sources` regenerates that branch, widens its initial-tuple clauses,
and requires the result to be byte-identical to the supplied reference encoding.
This establishes continuity between the canonical source and the checked DRAT
proofs without trusting graph names or rerunning SAT search.

## Checked object sizes

| Object | Variables | Clauses | Bytes |
| --- | ---: | ---: | ---: |
| Reference encoding | 984,882 | 4,756,124 | 118,444,022 |
| Combined core | 984,882 | 904,855 | 18,905,669 |
| Reduced encoding | 282,535 | 433,150 | 9,817,630 |
| Reference/combined binary DRAT | — | — | 1,749,789,591 |
| Reduced binary DRAT | — | — | 1,435,082,233 |

The supplied proof files previously produced `s VERIFIED` with zero RAT lemmas
in the reference environment.  Solver and checker performance is not used as
a premise of the theorem.
