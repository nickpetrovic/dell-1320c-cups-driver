#!/bin/sh
set -eu

golden=$(cd "$(dirname "$0")" && pwd)
filter=${FILTER:?set FILTER to the rastertodell1320c binary}
ppd=${PPD_FILE:?set PPD_FILE to the generated PPD}
known=$golden/known-differences
for tool in xz cmp sed; do
    command -v "$tool" >/dev/null 2>&1 || { echo "error: the golden tests need $tool (on Fedora, install xz and diffutils)" >&2; exit 2; }
done

work=$(mktemp -d "${TMPDIR:-/tmp}/dell1320c-golden.XXXXXX")
trap 'rm -rf "$work"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

normalize() {
    LC_ALL=C sed -E \
        -e '/^@PJL COMMENT (DATE|TIME)=/d' \
        -e '/^@PJL SET JOBATTR="@HOAD=/d' \
        -e 's/^(@PJL SET JOBATTR="@CNAM)=[^"]*"/\1=X"/' "$1"
}

is_known_difference() {
    [ -f "$known" ] && grep -qx "$1" "$known"
}

passed=0
failed=0
expected_failures=0
unexpected_passes=0
failures=

while IFS="$(printf '\t')" read -r name raster copies options; do
    [ -n "$name" ] || continue
    if [ ! -f "$work/$raster.ras" ]; then
        xz -dc "$golden/rasters/$raster.ras.xz" > "$work/$raster.ras"
    fi
    xz -dc "$golden/expected/$name.prn.xz" > "$work/expected.prn"
    if ! PPD=$ppd "$filter" 1 u t "$copies" "$options" "$work/$raster.ras" > "$work/actual.prn" 2> "$work/stderr"; then
        result=error
    else
        normalize "$work/expected.prn" > "$work/expected.normalized"
        normalize "$work/actual.prn" > "$work/actual.normalized"
        if cmp -s "$work/expected.normalized" "$work/actual.normalized"; then
            result=pass
        else
            result=fail
        fi
    fi

    if is_known_difference "$name"; then
        if [ "$result" = pass ]; then
            unexpected_passes=$((unexpected_passes + 1))
            failures="$failures $name"
            printf 'XPASS %s (now matches; remove it from known-differences)\n' "$name"
        else
            expected_failures=$((expected_failures + 1))
            printf 'XFAIL %s\n' "$name"
        fi
    elif [ "$result" = pass ]; then
        passed=$((passed + 1))
        printf 'PASS  %s\n' "$name"
    else
        failed=$((failed + 1))
        failures="$failures $name"
        printf 'FAIL  %s (%s)\n' "$name" "$result"
        [ "$result" = error ] && sed 's/^/      /' "$work/stderr"
    fi
done < "$golden/index.tsv"

letter_raster=$(awk -F '\t' '$1 == "letter" { print $2 }' "$golden/index.tsv")
letter_options=$(awk -F '\t' '$1 == "letter" { print $4 }' "$golden/index.tsv")
[ -n "$letter_raster" ] || { echo "error: index.tsv has no letter case for the truncated raster check" >&2; exit 2; }
head -c 100000 "$work/$letter_raster.ras" > "$work/truncated.ras"
if PPD=$ppd "$filter" 1 u t 1 "$letter_options" "$work/truncated.ras" > "$work/truncated.prn" 2> "$work/stderr"; then
    failures="$failures truncated-raster"
    printf 'FAIL  a truncated raster is reported as an error\n'
elif tail -c 16 "$work/truncated.prn" | grep -q '@PJL EOJ'; then
    printf 'PASS  a truncated raster is reported as an error and still ends the job\n'
else
    failures="$failures truncated-raster"
    printf 'FAIL  a truncated raster leaves the job unfinished\n'
fi

printf '\n%d passed, %d failed, %d known differences, %d unexpected passes\n' \
    "$passed" "$failed" "$expected_failures" "$unexpected_passes"
if [ -n "$failures" ]; then
    printf 'needs attention:%s\n' "$failures"
    exit 1
fi
