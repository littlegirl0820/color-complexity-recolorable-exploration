#!/usr/bin/env bash
set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
inputs=$root/reproduction/inputs

restore() {
  local name=$1
  local archive=$inputs/$name.gz
  local destination=$inputs/$name
  local temporary

  if [[ -r $destination ]]; then
    return
  fi
  if [[ ! -r $archive ]]; then
    echo "missing compressed input: $archive" >&2
    exit 2
  fi

  temporary=$(mktemp "$inputs/.${name}.XXXXXX")
  trap 'rm -f -- "$temporary"' RETURN
  gzip -dc -- "$archive" >"$temporary"
  chmod 0644 "$temporary"
  mv -- "$temporary" "$destination"
  trap - RETURN
}

restore v3-trace.blocks
restore v5-domain4.blocks

(cd "$root/reproduction" && sha256sum -c INPUT_SHA256SUMS)
echo "reproduction-inputs=READY"
