#!/bin/sh
set -eu

project=$(cd "$(dirname "$0")/../.." && pwd)
work=${WORK:-$(mktemp -d /tmp/dell1320c-color-tables.XXXXXX)}
vendor=/Library/Printers/FujiXerox/Filter/fxmhbpl.bundle/Contents/MacOS
vendor_ppd_gz="/Library/Printers/PPDs/Contents/Resources/FX DocuPrint C525 A-AP.gz"
output=${1:-$project/data/color}

[ -x "$vendor/FXM_CC" ] || { echo "error: the Fuji Xerox driver is not installed in $vendor" >&2; exit 1; }
arch -x86_64 /usr/bin/true 2>/dev/null || { echo "error: Rosetta is required to run the Fuji Xerox driver" >&2; exit 1; }

mkdir -p "$work"
gzcat "$vendor_ppd_gz" > "$work/vendor.ppd"
export PPD="$work/vendor.ppd"

uv run -q --isolated --with numpy python -I "$project/tools/color-tables/make_cube.py" "$work/cube.fxr"

for document_type in Normal Photo Presentation Web POP; do
    "$vendor/FXM_CC" 1 u t 1 "FXDocumentType=$document_type" "$work/cube.fxr" > "$work/cube-$document_type.fxr"
done
"$vendor/FXM_CC" 1 u t 1 "FXColorMode=Black" "$work/cube.fxr" > "$work/cube-Black.fxr"
"$vendor/FXM_CC" 1 u t 1 "FXColorMode=Black FXDocumentType=Off" "$work/cube.fxr" > "$work/cube-BlackOff.fxr"
"$vendor/FXM_CC" 1 u t 1 "FXDocumentType=Off" "$work/cube.fxr" > "$work/cube-Off.fxr"

if cmp -s "$work/cube.fxr" "$work/cube-Off.fxr"; then
    echo "Image Types Off leaves colors unchanged, so it needs no table"
else
    echo "warning: the vendor's Image Types Off setting now changes colors; this driver assumes it does not" >&2
fi

uv run -q --isolated --with numpy python -I "$project/tools/color-tables/make_tables.py" "$work" "$output"
echo "Tables written to $output. The raw recordings in $work can be deleted."
