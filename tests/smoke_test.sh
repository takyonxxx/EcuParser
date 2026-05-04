#!/bin/bash
# Smoke test: parse a random sample of .drt files and report anomalies.
# Used to catch format edge cases not present in J293_822.drt.

set -e
cd /home/claude/EcuParser
EXE=./build/EcuParser

# Sample 200 random drivers
DRTS=$(ls /home/claude/extracted/DataBase/DRT/*.drt | shuf -n 200)

OK=0
FAIL=0
FAIL_FILES=""

for f in $DRTS; do
    if $EXE --drt "$f" >/dev/null 2>&1; then
        OK=$((OK+1))
    else
        FAIL=$((FAIL+1))
        FAIL_FILES="$FAIL_FILES $(basename $f)"
    fi
done

echo "OK:   $OK"
echo "FAIL: $FAIL"
if [ $FAIL -gt 0 ]; then
    echo "Failing files (first 10):"
    echo "$FAIL_FILES" | tr ' ' '\n' | head -10
fi
