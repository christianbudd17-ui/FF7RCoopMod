#!/bin/sh
# f1.85: runs unit_settings.exe on every ini in settings_cases/ (and with no ini = the written default) under wine.
# usage: sh tools/tests/run_settings.sh <unit_settings.exe> <outdir>   - then diff two outdirs (ignore the "loaded from" path line)
EXE=$(realpath "$1"); OUT=$(realpath -m "$2"); HERE=$(dirname "$(realpath "$0")"); mkdir -p "$OUT"
for v in none $(cd "$HERE/settings_cases" && ls *.ini | sed 's/\.ini$//'); do
  D=$(mktemp -d); cp "$EXE" "$D/t.exe"
  [ "$v" != none ] && cp "$HERE/settings_cases/$v.ini" "$D/FF7RCoopMod.ini"
  (cd "$D" && WINEDEBUG=-all wine t.exe 2>/dev/null | grep -v "^wine:" > "$OUT/$v.dump")
  [ "$v" = none ] && cp "$D/FF7RCoopMod.ini" "$OUT/default_written.ini"
  sed 's/^\[[0-9:.]*\] //; s/tmp\.[A-Za-z0-9]*/tmp.X/' "$D/FF7RCoopMod_Coop.log" > "$OUT/$v.log" 2>/dev/null
  rm -rf "$D"
done
