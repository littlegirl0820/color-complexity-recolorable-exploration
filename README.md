# Source and verification code for the three-color lower bound

This repository contains the source code and fixed inputs used to verify the
three-color impossibility result for subcubic pseudotrees. It accompanies the
paper

> Shoma Hiraoka, Shunsuke Imori, Shota Takahashi, and Yuichi Sudo.
> *Color Complexity of Recolorable Graph Exploration: Upper and Lower Bounds
> via Block Structure.*

The main result checked here is that no single deterministic three-color rule
explores every graph in a fixed family of nine subcubic pseudotrees. The proof
has two parts:

1. a direct checker reduces the 27 possible initial color tuples to four
   representatives; and
2. a finite-state SAT encoding rules out each remaining representative.

Large generated CNF and DRAT files are not stored in this source repository.
They are regenerated from the included fixed inputs. No separate
precomputed-proof archive is part of this distribution.

## Start here

The quick check requires only Bash and Node.js:

```sh
bash ./verify.sh sources
```

It verifies the nine obstruction graphs and the direct initial-tuple
reduction. The final output should include:

```text
obstruction-family=VERIFIED graphs=9 trees=2 unicyclic=7
initial-tuple-reduction=VERIFIED total=27 direct=19 residual=8
source-snapshot-checks=VERIFIED
lower-bound-certificate=NOT CHECKED (quick checks only; use reproduction/regenerate_and_verify.sh for full verification)
```

This quick command does not prove that the SAT instances are unsatisfiable.
The table below distinguishes the available verification paths.

## Verification paths

| Path | Command | Typical cost | What it checks |
| --- | --- | --- | --- |
| Quick source check | `bash ./verify.sh sources` | seconds | obstruction family and direct reduction from 27 tuples |
| CNF regeneration | `bash ./reproduction/regenerate_and_verify.sh OUT --cnf-only --jobs 4` | disk-intensive | policy clauses, generator, and the hashes of four regenerated CNFs |
| Full regeneration | `bash ./reproduction/regenerate_and_verify.sh OUT --jobs 4` | about half a day | regenerates, solves, and checks all four remaining SAT instances |

`OUT` must name a directory that does not yet exist. Put it outside this
repository because the generated CNF and DRAT files require several gigabytes.

## Requirements

The quick source check needs:

- Bash;
- Node.js.

CNF regeneration additionally needs:

- a C++20 compiler (`g++`);
- `sha256sum`;
- gzip and standard Unix command-line utilities;
- about 7 GB of free disk space.

Full regeneration additionally needs executable copies of `CaDiCaL` and
`drat-trim`. With four parallel branches, about 10 GB of memory is a practical
estimate. Runtime and memory use depend on the machine.

## Regenerate the four SAT branches

The two largest fixed policy files are stored as deterministic gzip archives
so that every repository file remains below the GitHub file-size limit.
The regeneration wrapper restores and hash-checks them
automatically. They can also be restored explicitly:

```sh
bash ./prepare_reproduction_inputs.sh
```

To regenerate the four CNFs without running a SAT solver:

```sh
bash ./reproduction/regenerate_and_verify.sh /path/to/cnf-output \
  --cnf-only --jobs 4
```

To regenerate and verify all four UNSAT proofs:

```sh
bash ./reproduction/regenerate_and_verify.sh /path/to/full-output \
  --jobs 4 \
  --cadical /path/to/cadical \
  --drat-trim /path/to/drat-trim
```

The run succeeds only if every regenerated CNF has its expected SHA-256,
CaDiCaL reports UNSAT for all four branches, and `drat-trim` verifies every
proof. The final line is:

```text
four-branch-lower-bound=VERIFIED output=...
```

See [`reproduction/README.md`](reproduction/README.md) for resource estimates
and the exact outputs retained in the result directory.

## Previously checked proof objects

Appendix A also describes a previously checked reference CNF, a combined
contradiction core, a reduced CNF, and the corresponding DRAT proofs. Their
principal hashes are recorded in
[`certificate/SHA256SUMS`](certificate/SHA256SUMS). These large files are not
included in this distribution. The four-branch procedure above verifies the
same lower bound without them; it does not reconstruct the combined or reduced
objects byte for byte.

For a holder of the original precomputed-proof bundle, the optional assembly
script creates a layout with its own `README.md`. From that layout, run:

```sh
bash ./verify.sh full /path/to/drat-trim
```

The discovery solver is not part of the trusted base. Deterministic checkers
validate the graph instances, the imported policy clauses, and the exact
source-to-CNF construction. `drat-trim` then checks unsatisfiability directly
against the reference encoding.

## Additional checks

`checker/simulate_algorithms.py {tc3|c4|us5} N` exhaustively simulates the three
exploration algorithms against every adversarial choice from every start on all
labeled graphs of their classes with at most `N` vertices.  `sanity_positive_control.sh`
checks that the generator's encoding of the tree-and-cycle subfamily, including merge clauses, is
satisfiable, stays satisfiable with the three-color rule fixed, and becomes
unsatisfiable when the degree-one all-zero action is replaced by a stop
(requires `cadical`).

## Repository layout

- `verify.sh`: quick checks and the verification entry point used by the full artifact;
- `verify_obstruction_family.mjs`: checks the nine graph instances;
- `verify_initial_tuple_reduction.mjs`: checks the direct reduction of initial tuples;
- `checker/`: independent policy-clause checkers and the exhaustive algorithm simulator;
- `generator/`: finite-state CNF generator and tuple-widening tool;
- `reproduction/`: fixed inputs and the four-branch regeneration wrapper;
- `certificate/`: expected hashes for the large proof objects;
- `sanity_positive_control.sh`: positive and negative controls for the CNF generator;
- `assemble_anonymous_artifact.sh`: constructs the full artifact from a local certificate bundle.

The repository contains no solver logs, generated CNFs, generated DRAT files,
or local filesystem paths.
