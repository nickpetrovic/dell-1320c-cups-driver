#!/bin/sh
set -eu

package=${1:?usage: print-test.sh <package> <document.pdf> <page count>}
document=${2:?usage: print-test.sh <package> <document.pdf> <page count>}
expected_pages=${3:?usage: print-test.sh <package> <document.pdf> <page count>}
queue=Dell_Color_Laser_1320c
output=/tmp/printer.prn
failures=0

fail() {
    printf 'FAIL  %s\n' "$*"
    failures=$((failures + 1))
}

pass() {
    printf 'PASS  %s\n' "$*"
}

install_package() {
    case $package in
        *.deb)
            export DEBIAN_FRONTEND=noninteractive
            apt-get update -qq >/dev/null
            apt-get install -y -qq "$package" netcat-openbsd >/dev/null 2>&1
            ;;
        *.rpm)
            dnf install -y -q "$package" nmap-ncat >/dev/null 2>&1
            ;;
        *.tar.gz)
            export DEBIAN_FRONTEND=noninteractive
            apt-get update -qq >/dev/null
            apt-get install -y -qq cups netcat-openbsd >/dev/null 2>&1
            tar -xzf "$package" -C /
            ;;
        *)
            echo "unknown package type: $package" >&2
            exit 2
            ;;
    esac
}

wait_for_job() {
    listener=$1
    seconds=0
    while kill -0 "$listener" 2>/dev/null && [ "$seconds" -lt 180 ]; do
        sleep 1
        seconds=$((seconds + 1))
    done
    if kill -0 "$listener" 2>/dev/null; then
        kill "$listener"
        return 1
    fi
    return 0
}

install_package
system=$(sed -n 's/^PRETTY_NAME="\{0,1\}\([^"]*\)"\{0,1\}$/\1/p' /etc/os-release)
printf '%s, %s, %s\n' "$system" "$(uname -m)" "$(basename "$package")"

mkdir -p /run/cups
cupsd
seconds=0
until LC_ALL=C lpstat -r 2>/dev/null | grep -qx 'scheduler is running'; do
    [ "$seconds" -lt 60 ] || { echo "CUPS did not start within a minute" >&2; exit 1; }
    sleep 1
    seconds=$((seconds + 1))
done

nc -l 9100 > "$output" &
listener=$!
dell1320c add 127.0.0.1 --protocol socket --paper Letter --default >/dev/null
lp -o FXDocumentType=Photo "$document" >/dev/null

if wait_for_job "$listener"; then
    pass "the printer received $(wc -c < "$output" | tr -d ' ') bytes"
else
    fail "the printer received nothing within 3 minutes"
fi

if head -c 31 "$output" | grep -q '@PJL JOB MODE=PRINTER'; then
    pass "the job starts with the PJL job header"
else
    fail "the job does not start with the PJL job header"
fi

pages=$(grep -a -c 'PAGEINFO=SIZE\[215.9X279.4\]:MODE\[COLOR\]' "$output" || true)
if [ "$pages" -eq "$expected_pages" ]; then
    pass "the trailer describes $pages Letter color pages"
else
    fail "the trailer describes $pages pages instead of $expected_pages"
fi

if grep -a -q '@PJL SET JOBATTR="@TCPR=22"' "$output"; then
    pass "the Photo color settings are in the job"
else
    fail "the Photo color settings are missing"
fi

if tail -c 16 "$output" | grep -a -q '@PJL EOJ'; then
    pass "the job ends with EOJ"
else
    fail "the job does not end with EOJ"
fi

nc -l 9100 > "$output" &
listener=$!
lp -o print-color-mode=monochrome -o print-quality=3 "$document" >/dev/null
if wait_for_job "$listener" && grep -a -q 'MODE\[MONOCHROME\]' "$output" && grep -a -q '@PJL SET RESOLUTION=300' "$output"; then
    pass "IPP print-color-mode and print-quality select black and white and 300 dpi"
else
    fail "IPP print-color-mode and print-quality had no effect"
fi

lpadmin -p "$queue" -o PageSize=A4 -o FXTonerSaver=True -o FXColorMode=Black -o FXOutputMode=300x300
lpoptions -p "$queue" -o Duplex=DuplexNoTumble
dell1320c refresh >/dev/null
lpoptions -p "$queue" -r Duplex
queue_ppd="/etc/cups/ppd/$queue.ppd"
if grep -qx '\*DefaultPageSize: A4' "$queue_ppd" && grep -qx '\*DefaultFXTonerSaver: True' "$queue_ppd" &&
    grep -qx '\*DefaultFXColorMode: Black' "$queue_ppd" && grep -qx '\*DefaultFXOutputMode: 300x300' "$queue_ppd" &&
    grep -qx '\*DefaultDuplex: None' "$queue_ppd"; then
    pass "refresh keeps the queue's paper size, toner saver, color mode and resolution, and not root's own options"
else
    fail "refresh lost the chosen options"
fi

lpadmin -p "$queue" -o PageSize=Custom.400x600
dell1320c refresh >/dev/null
if grep -q '^\*DefaultPageSize: Custom.400x600' "/etc/cups/ppd/$queue.ppd"; then
    pass "refresh keeps a custom default paper size"
else
    fail "refresh changed the custom default paper size to $(sed -n 's/^\*DefaultPageSize: *//p' "/etc/cups/ppd/$queue.ppd")"
fi

if [ "$failures" -gt 0 ]; then
    printf '%d checks failed\n' "$failures"
    exit 1
fi
