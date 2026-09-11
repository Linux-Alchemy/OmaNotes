#!/usr/bin/env bash
# Verify the hardening that leaves a mark in the ELF (cmake/Warnings.cmake,
# docs/threat-model.md F-8). Run it against the release binary:
#
#   cmake --build --preset release --target security-check
#
# Against the dev binary it still checks what it can, and says so.
set -euo pipefail

binary=${1:?usage: security-check.sh <binary>}

fail() {
  printf 'security-check: %s\n' "$1" >&2
  exit 1
}

if [[ ! -x "$binary" ]]; then
  fail "executable not found: $binary"
fi

headers=$(readelf -hW "$binary")
segments=$(readelf -lW "$binary")
dynamic=$(readelf -dW "$binary")
symbols=$(readelf --dyn-syms -W "$binary")
notes=$(readelf -nW "$binary")

elf_type=$(awk '/Type:/ { print $2; exit }' <<<"$headers")
[[ "$elf_type" == "DYN" ]] || fail "expected PIE (DYN), found $elf_type"

grep -q 'GNU_RELRO' <<<"$segments" || fail "GNU_RELRO segment missing"
grep -q 'BIND_NOW' <<<"$dynamic" || fail "immediate binding (BIND_NOW) missing; RELRO is partial"

stack_flags=$(awk '/GNU_STACK/ { print $7; exit }' <<<"$segments")
[[ "$stack_flags" != *E* ]] || fail "executable stack detected"

if grep -Eq 'RPATH|RUNPATH' <<<"$dynamic"; then
  fail "unexpected RPATH/RUNPATH detected"
fi

grep -q '__stack_chk_fail' <<<"$symbols" || fail "stack protector missing (__stack_chk_fail not referenced)"

sanitized=false
if grep -q '__asan_init' <<<"$symbols"; then
  sanitized=true
fi

# CET marks: the linker keeps IBT and SHSTK only if every object carried
# them. The sanitizer runtime is linked in statically and carries none, so
# a dev binary cannot show them; the release binary must.
if grep -q 'IBT' <<<"$notes" && grep -q 'SHSTK' <<<"$notes"; then
  cet="IBT+SHSTK"
elif [[ "$sanitized" == true ]]; then
  cet="not applicable (sanitizer runtime carries no CET marks)"
else
  fail "CET marks (IBT and SHSTK) missing"
fi

# Fortified calls only exist where the compiler could not prove a size, so
# their presence in an optimised build proves the flag. A sanitizer build
# is Debug, where _FORTIFY_SOURCE is deliberately off; any checked calls it
# shows come from the runtime, not from this code, so it is not evaluated.
if [[ "$sanitized" == true ]]; then
  fortified="not evaluated (Debug build; _FORTIFY_SOURCE needs optimisation)"
elif grep -Eq ' __[a-z0-9_]+_chk(@| |$)' <<<"$(grep -v '__stack_chk_fail' <<<"$symbols")"; then
  fortified=yes
else
  fail "no fortified libc calls found; _FORTIFY_SOURCE is not in effect"
fi

printf 'security-check: PIE, full RELRO, NX stack, no RPATH, stack protector: PASS\n'
printf 'security-check: CET: %s\n' "$cet"
printf 'security-check: fortify: %s\n' "$fortified"
if [[ "$sanitized" == true ]]; then
  printf 'security-check: note: this is a sanitizer build; release evidence comes from the release preset\n'
fi
