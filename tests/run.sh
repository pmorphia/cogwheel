#!/bin/sh
# Runs every port's tests and the conformance cases (tests/cases/*.cw against *.out).
# Each tool is optional: set FLYWHEEL, FLYWHEEL_LITE, FLYWHEEL_JS (path to bin/flywheel-js.js),
# CC, CXX, PYTHON, or put them on PATH; ports whose tool is missing are skipped.
set -u
cd "$(dirname "$0")/.."
FLYWHEEL=${FLYWHEEL:-flywheel}
FLYWHEEL_LITE=${FLYWHEEL_LITE:-flywheel-lite}
FLYWHEEL_JS=${FLYWHEEL_JS:-}
CC=${CC:-cc}
CXX=${CXX:-c++}
PYTHON=${PYTHON:-python3}
out=$(mktemp -d)
trap 'rm -rf "$out"' EXIT
failed=0

have() { command -v "$1" >/dev/null 2>&1; }
conform() { # name, then the command that dumps a case
    name=$1
    shift
    bad=0
    for case in tests/cases/*.cw; do
        if "$@" "$case" >"$out/got" 2>&1 && cmp -s "$out/got" "${case%.cw}.out"; then
            :
        else
            echo "FAIL $name $case"
            diff "$out/got" "${case%.cw}.out" | head -20
            failed=1
            bad=1
        fi
    done
    [ "$bad" = 0 ] && echo "ok   $name conformance"
}

if have "$FLYWHEEL"; then
    "$FLYWHEEL" test tests/ || failed=1
    conform flywheel sh -c '"$0" run tests/dump.fw -- "$1"' "$FLYWHEEL"
fi
if have "$FLYWHEEL_LITE"; then
    "$FLYWHEEL_LITE" test tests/ || failed=1
    conform flywheel-lite "$FLYWHEEL_LITE" tests/dump.fw
fi
if [ -n "$FLYWHEEL_JS" ] && have node; then
    conform flywheel.js node "$FLYWHEEL_JS" tests/dump.fw
fi
if have "$CC"; then
    "$CC" -std=c99 -Wall -Wextra -o "$out/ctest" c/cogwheel.c c/test.c && "$out/ctest" || failed=1
    "$CC" -std=c99 -O2 -o "$out/cdump" c/cogwheel.c c/dump.c && conform c "$out/cdump"
fi
if have "$CXX"; then
    "$CXX" -std=c++17 -Wall -Wextra -o "$out/cpptest" cpp/test.cpp && "$out/cpptest" || failed=1
    "$CXX" -std=c++17 -O2 -o "$out/cppdump" cpp/dump.cpp && conform c++ "$out/cppdump"
fi
if [ -d pycogwheel ] && have "$PYTHON"; then
    PYTHONPATH=pycogwheel/src "$PYTHON" -m unittest discover -s pycogwheel/tests -q || failed=1
fi
[ "$failed" = 0 ] && echo "all passed"
exit "$failed"
