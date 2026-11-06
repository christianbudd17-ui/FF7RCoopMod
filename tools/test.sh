#!/bin/sh
# tools/test.sh - builds and runs every unit test (no game needed), stops at the first failure (restructure finding #18).
# Run from the fork1 folder (where build.sh is):   sh tools/test.sh
# Needs the same compiler as build.sh (x86_64-w64-mingw32-g++-posix) and wine.
#   sh tools/test.sh                  - all tests
#   sh tools/test.sh settings OLDDIR  - also compare the settings dumps with an earlier run's folder (see below)
# The settings test (unit_settings) loads every ini in tools/tests/settings_cases/ (and no ini = the default) and writes
# what it read to out/settings/<case>.dump / .log. To prove a change didn't alter the settings reading: run this before
# the change, copy out/settings somewhere, change the code, run again with "settings <that copy>".
set -e
CXX="x86_64-w64-mingw32-g++-posix -O1 -static"
OUT=out/tests; mkdir -p "$OUT"
export WINEDEBUG=-all
fail() { echo "FAILED: $1"; exit 1; }

run() {   # run <name> <extra source files / libraries...> - expects the last line "ALL OK..."
  name=$1; shift
  echo "-- $name"
  $CXX -o "$OUT/$name.exe" "tools/tests/$name.cpp" "$@" || fail "$name (build)"
  last=$(cd "$OUT" && wine "./$name.exe" 2>/dev/null | tr -d '\r' | tail -1)
  echo "   $last"
  case "$last" in "ALL OK"*) ;; *) fail "$name" ;; esac
}

run unit_map
run unit_output
run unit_monitors
run unit_slots
run unit_pause
echo "-- unit_log (f1.207: the log kept / set aside at 20 MB at launch, 40 MB in a session - each case its own process)"
$CXX -o "$OUT/unit_log.exe" tools/tests/unit_log.cpp || fail "unit_log (build)"
mkdir -p "$OUT/log"
for c in small big session; do
  (cd "$OUT/log" && wine ../unit_log.exe setup_$c >/dev/null 2>&1)
  last=$(cd "$OUT/log" && wine ../unit_log.exe $c 2>/dev/null | tr -d '\r' | tail -1)
  echo "   $last"
  case "$last" in "ALL OK"*) ;; *) (cd "$OUT/log" && wine ../unit_log.exe $c 2>/dev/null); fail "unit_log $c" ;; esac
done
rm -rf "$OUT/log"
IN2="input2/in2.cpp input2/in2_hid.cpp input2/in2_dinput.cpp input2/in2_xinput.cpp"
run unit_in2 $IN2 -ldinput8 -ldxguid
run unit_seats -DIN2_TEST_HOOKS $IN2 input2/seats.cpp -ldinput8 -ldxguid

echo "-- smoke_in2 (starts the controller module for a moment - only checks it runs)"
$CXX -o "$OUT/smoke_in2.exe" tools/tests/smoke_in2.cpp input2/*.cpp -ldinput8 -ldxguid -lhid -lsetupapi || fail "smoke_in2 (build)"
last=$(cd "$OUT" && timeout 60 wine ./smoke_in2.exe 2>/dev/null | tr -d '\r' | tail -1)
echo "   $last"
[ "$last" = "done" ] || fail smoke_in2

echo "-- unit_settings (every case in tools/tests/settings_cases/)"
$CXX -o "$OUT/unit_settings.exe" tools/tests/unit_settings.cpp || fail "unit_settings (build)"
rm -rf out/settings
sh tools/tests/run_settings.sh "$OUT/unit_settings.exe" out/settings
for f in out/settings/*.dump; do [ -s "$f" ] || fail "unit_settings: $f is empty"; done
echo "   $(ls out/settings/*.dump | wc -l) case(s) read"
if [ "$1" = "settings" ] && [ -n "$2" ]; then
  diff -r "$2" out/settings > /dev/null || { diff -r "$2" out/settings | head -20; fail "settings dumps differ from $2"; }
  echo "   same as $2"
fi
echo "ALL TESTS OK"
