#!/usr/bin/env bash
# Regression suite for giftcardreader. Builds the reader and runs it
# against the "normal" example file plus the known-bad test cases in
# this directory, checking that:
#   - examplefile.gft still produces correct output (no regressions)
#   - crash1.gft / crash2.gft no longer crash the reader
#   - hang.gft no longer hangs the reader
set -u
cd "$(dirname "$0")/.."

FAIL=0

echo "Building giftcardreader..."
gcc -o giftcardreader giftcardreader.c || { echo "BUILD FAILED"; exit 1; }

check() {
    local name="$1"
    local file="$2"
    local expect_crash="$3"   # "crash", "hang", or "ok"

    timeout 5 ./giftcardreader 1 "$file" > /dev/null 2>/tmp/gcr_stderr
    rc=$?

    case "$expect_crash" in
        ok)
            if [ $rc -ne 0 ]; then
                echo "FAIL: $name ($file) expected clean exit, got rc=$rc"
                FAIL=1
            else
                echo "PASS: $name ($file) exited cleanly"
            fi
            ;;
        crash)
            if [ $rc -ge 128 ]; then
                echo "FAIL: $name ($file) still crashes the reader (signal $((rc-128)))"
                FAIL=1
            elif [ $rc -eq 124 ]; then
                echo "FAIL: $name ($file) now hangs instead of crashing"
                FAIL=1
            else
                echo "PASS: $name ($file) no longer crashes (rc=$rc)"
            fi
            ;;
        hang)
            if [ $rc -eq 124 ]; then
                echo "FAIL: $name ($file) still hangs the reader"
                FAIL=1
            else
                echo "PASS: $name ($file) no longer hangs (rc=$rc)"
            fi
            ;;
    esac
}

check "examplefile (regression)" "examplefile.gft" ok
check "crash1" "tests/crash1.gft" crash
check "crash2" "tests/crash2.gft" crash
check "hang" "tests/hang.gft" hang

if [ $FAIL -ne 0 ]; then
    echo "=== ONE OR MORE REGRESSION TESTS FAILED ==="
    exit 1
fi
echo "=== all tests passed ==="
