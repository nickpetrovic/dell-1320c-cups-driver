#!/bin/bash
set -euo pipefail

project=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
golden=$project/test/golden
documents=$project/test/documents
vendor=/Library/Printers/FujiXerox/Filter/fxmhbpl.bundle/Contents/MacOS
vendor_ppd_gz="/Library/Printers/PPDs/Contents/Resources/FX DocuPrint C525 A-AP.gz"
rasterizer=/usr/libexec/cups/filter/cgpdftoraster
work=$(mktemp -d /tmp/dell1320c-record.XXXXXX)
trap 'rm -rf "$work"' EXIT

[ -x "$vendor/FXM_HBPL" ] || { echo "error: Fuji Xerox driver not found in $vendor" >&2; exit 1; }
arch -x86_64 /usr/bin/true 2>/dev/null || { echo "error: Rosetta is required to run the Fuji Xerox driver" >&2; exit 1; }
[ -x "$rasterizer" ] || { echo "error: $rasterizer not found (macOS only)" >&2; exit 1; }

gzcat "$vendor_ppd_gz" \
    | sed -e 's/^\*DefaultOption1: .*/*DefaultOption1: 1Tray-S/' \
        -e 's/^\*DefaultFXInputSlot: .*/*DefaultFXInputSlot: 1stTray-S/' > "$work/vendor.ppd"
export PPD=$work/vendor.ppd

document_path() {
    if [ "$1" = photo.pdf ]; then
        if [ ! -s "$work/photo.pdf" ]; then
            local image
            image=$(find "/System/Library/Desktop Pictures" -maxdepth 1 -name "*.heic" | sort | head -1)
            sips -s format pdf "$image" --out "$work/photo.pdf" >/dev/null
        fi
        echo "$work/photo.pdf"
    elif [ "$1" = noise.pdf ]; then
        if [ ! -s "$work/noise.pdf" ]; then
            uv run -q --isolated python -I "$project/tools/golden/make_noise.py" "$work/noise.png"
            sips -s format pdf "$work/noise.png" --out "$work/noise.pdf" >/dev/null
        fi
        echo "$work/noise.pdf"
    else
        echo "$documents/$1"
    fi
}

rm -rf "$golden/rasters" "$golden/expected"
mkdir -p "$golden/rasters" "$golden/expected"
: > "$golden/index.tsv"

while IFS=$'\t' read -r name document copies options; do
    [ -n "$name" ] || continue
    "$rasterizer" 1 u t "$copies" "$options" "$(document_path "$document")" > "$work/page.ras" 2>/dev/null
    raster=$(shasum -a 256 "$work/page.ras" | cut -c1-16)
    [ -f "$golden/rasters/$raster.ras.xz" ] || xz -9 -c "$work/page.ras" > "$golden/rasters/$raster.ras.xz"

    "$vendor/FXM_FXR" 1 u t "$copies" "$options" "$work/page.ras" \
        | "$vendor/FXM_SBP" 1 u t "$copies" "$options" \
        | "$vendor/FXM_PR" 1 u t "$copies" "$options" \
        | "$vendor/FXM_CC" 1 u t "$copies" "$options" \
        | "$vendor/FXM_ALC" 1 u t "$copies" "$options" \
        | "$vendor/FXM_HBPL" 1 u t "$copies" "$options" > "$work/vendor.prn" 2>/dev/null
    xz -9 -c "$work/vendor.prn" > "$golden/expected/$name.prn.xz"

    printf '%s\t%s\t%s\t%s\n' "$name" "$raster" "$copies" "$options" >> "$golden/index.tsv"
    printf '%-40s raster %s  %9d bytes\n' "$name" "$raster" "$(stat -f %z "$work/vendor.prn")"
done < "$golden/cases.tsv"

du -sh "$golden/rasters" "$golden/expected"
