#!/bin/sh
# Fail if a built plugin needs a newer glibc than Bitwig's Flatpak runtime has.
# Usage: ./check-glibc.sh [max minor version, default 42]
MAX=${1:-42}
status=0
for f in target/bundled/*.clap target/bundled/*.vst3/Contents/x86_64-linux/*.so; do
  [ -f "$f" ] || continue
  need=$(objdump -T "$f" | grep -oE 'GLIBC_2\.[0-9]+' | cut -d. -f2 | sort -n | tail -1)
  if [ "$need" -gt "$MAX" ]; then
    echo "FAIL $f needs GLIBC_2.$need:"; objdump -T "$f" | grep -E "GLIBC_2\.$need\b" | awk '{print "   ", $NF}'
    status=1
  else
    echo "ok   $f (needs up to GLIBC_2.$need)"
  fi
done
exit $status
