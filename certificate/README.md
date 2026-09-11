# Certificate manifest

This directory records expected hashes for five previously checked proof
objects described in Appendix A.  These objects are not included in the source
repository, and no separate precomputed-proof archive is part of this
distribution.  The two DRAT files alone occupy about 3.2 GB.

To verify the lower bound using only the distributed source and fixed inputs,
follow `../reproduction/README.md`.  That route regenerates and checks the four
representative branches.  It does not reconstruct the combined or reduced
objects listed here byte for byte, so this manifest is not a manifest of its
newly generated proofs.

For a holder of the original precomputed-proof bundle, the five principal
objects can be checked from the assembled artifact root with:

```sh
sha256sum -c /path/to/source-repository/certificate/SHA256SUMS
```

That bundle can be converted into the optional verification layout with:

```sh
bash ./assemble_anonymous_artifact.sh SOURCE_CERTIFICATE_BUNDLE OUTPUT_DIRECTORY
```

The assembly script also computes a complete manifest for every file in the
artifact and runs the deterministic source-to-CNF checks.  DRAT checking is a
separate, explicitly requested operation; see `../ARTIFACT_README.md`.
