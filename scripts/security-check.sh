#!/usr/bin/env bash
set -euo pipefail

binary=${1:?usage: security-check.sh <binary>}

if [[ ! -x "$binary" ]]; then
  printf 'security-check: executable not found: %s\n' "$binary" >&2
  exit 1
fi

elf_type=$(readelf -h "$binary" | awk '/Type:/ { print $2; exit }')
[[ "$elf_type" == "DYN" ]] || {
  printf 'security-check: expected PIE (DYN), found %s\n' "$elf_type" >&2
  exit 1
}

readelf -lW "$binary" | grep -q 'GNU_RELRO' || {
  printf 'security-check: GNU_RELRO segment missing\n' >&2
  exit 1
}

readelf -dW "$binary" | grep -q 'BIND_NOW' || {
  printf 'security-check: immediate binding (BIND_NOW) missing\n' >&2
  exit 1
}

stack_flags=$(readelf -lW "$binary" | awk '/GNU_STACK/ { print $7; exit }')
[[ "$stack_flags" != *E* ]] || {
  printf 'security-check: executable stack detected\n' >&2
  exit 1
}

if readelf -dW "$binary" | grep -Eq 'RPATH|RUNPATH'; then
  printf 'security-check: unexpected RPATH/RUNPATH detected\n' >&2
  exit 1
fi

printf 'security-check: PIE, RELRO, BIND_NOW, NX stack, and no RPATH: PASS\n'
