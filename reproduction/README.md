# Regenerating the lower-bound proof from the four branches

This directory contains the fixed inputs and the wrapper script for
regenerating the large CNF and DRAT proofs of the three-color lower bound on
a local machine.  These inputs and scripts are available in this source
repository.  The large generated objects
themselves are not included, and no separate precomputed-proof archive is
needed for this procedure.

By the initial-tuple reduction in the paper, the 27 possible initial tuples
reduce to the four representatives `111`, `121`, `211`, and `221`.  For each
representative, the wrapper performs four steps:

1. it checks the obstruction family, the initial-tuple reduction, and the
   fixed policy clauses with the independent checkers;
2. it generates the branch CNF from source and compares its SHA-256 with the
   value recorded at distribution time in `EXPECTED_CNF_SHA256SUMS`;
3. it produces a DRAT proof of unsatisfiability with CaDiCaL;
4. it checks each proof with `drat-trim`.

If all four representatives are unsatisfiable, then together with the
initial-tuple reduction this confirms that no common three-color rule exists.

## Requirements

The wrapper uses the following commands:

- Bash;
- Node.js;
- a C++20 compiler (`g++`);
- `sha256sum`;
- gzip and standard Unix command-line utilities;
- a CaDiCaL executable;
- a `drat-trim` executable.

CaDiCaL must be supplied as a runnable command, not as a header or library.
The fixed policy clauses are pregenerated proof inputs; the bundled checkers
verify their semantic validity, and the search that discovered them is not
rerun.

The two largest policy inputs are stored as deterministic gzip archives so
that every repository file stays below the GitHub file-size limit.
The wrapper restores and hash-checks them automatically against
`INPUT_SHA256SUMS`.  They can also be restored explicitly from the root of
this source tree:

```sh
bash ./prepare_reproduction_inputs.sh
```

## Full regeneration and checking

Pass an output directory that does not exist yet, placed outside this
repository:

```sh
bash ./reproduction/regenerate_and_verify.sh /path/to/output \
  --jobs 4 \
  --cadical /path/to/cadical \
  --drat-trim /path/to/drat-trim
```

When `cadical` and `drat-trim` are on `PATH`, the two path options can be
omitted:

```sh
bash ./reproduction/regenerate_and_verify.sh /path/to/output --jobs 4
```

With `--jobs 4`, expect about half a day in the reference environment, about
7 GB of free disk space, and about 10 GB of memory.  To reduce memory use,
`--jobs 1` runs the branches sequentially; the reference run took about
20 hours.  Runtime and memory depend on the machine.

When every check succeeds, the last line is:

```text
four-branch-lower-bound=VERIFIED output=...
```

The generated CNFs, DRAT proofs, solver logs, checker logs, and SHA-256
manifests are all retained in the chosen output directory.

## CNF generation only

The input checks and the generation of the four CNFs can also be run without
a SAT solver or DRAT check:

```sh
bash ./reproduction/regenerate_and_verify.sh /path/to/cnf-output \
  --cnf-only --jobs 4
```

This mode needs neither CaDiCaL nor `drat-trim`.  On success the last line is:

```text
four-branch-cnf-regeneration=VERIFIED output=...
```

## Relation to the previously checked proof objects

This route solves the four representative CNFs directly and checks the
three-color lower bound from scratch.  It does not regenerate the combined
contradiction core or the reduced encoding described in Appendix A, so it is
not a byte-for-byte reconstruction of the objects recorded in
`../certificate/SHA256SUMS`.  Those objects are not part of this distribution.
This procedure is an independent four-branch confirmation of the same lower
bound.
