#!/bin/sh
set -eu
umask 022

project=$(cd "$(dirname "$0")/../.." && pwd)
packaging=$project/packaging/macos
version=${VERSION:-$(cat "$project/VERSION")}
package_id=${PACKAGE_ID:?set PACKAGE_ID to the installer package identifier}
build=${BUILD:-build}
case $build in
    /*) ;;
    *) build=$project/$build ;;
esac
work=$build/macos-package
dist=$build/dist
output=$dist/Dell1320c-$version.pkg

archive() {
    (cd "$1" && find . | LC_ALL=C sort | cpio -o --quiet --format odc -R 0:0 | gzip -9 -n) > "$2"
}

bill_of_materials() {
    (
        cd "$1"
        find . | LC_ALL=C sort | while IFS= read -r path; do
            mode=$(stat -f %p "$path")
            if [ -d "$path" ]; then
                printf '%s\t%s\t0/0\n' "$path" "$mode"
            else
                read -r checksum size _ <<CHECKSUM
$(cksum < "$path")
CHECKSUM
                printf '%s\t%s\t0/0\t%s\t%s\n' "$path" "$mode" "$size" "$checksum"
            fi
        done
    ) > "$2.list"
    mkbom -i "$2.list" "$2"
    rm "$2.list"
}

rm -rf "$work"
mkdir -p "$work/root" "$work/scripts" "$work/component" "$work/resources" "$dist"

make -C "$project" BUILD="$build" DESTDIR="$work/root" install >/dev/null
install -m 755 "$packaging/postinstall" "$work/scripts/postinstall"

file_count=$(find "$work/root" | wc -l | tr -d ' ')
size_kilobytes=$(du -sk "$work/root" | cut -f1)
cat > "$work/component/PackageInfo" <<INFO
<?xml version="1.0" encoding="utf-8"?>
<pkg-info format-version="2" identifier="$package_id" version="$version" install-location="/" auth="root">
    <payload numberOfFiles="$file_count" installKBytes="$size_kilobytes"/>
    <scripts>
        <postinstall file="./postinstall"/>
    </scripts>
</pkg-info>
INFO
archive "$work/root" "$work/component/Payload"
archive "$work/scripts" "$work/component/Scripts"
bill_of_materials "$work/root" "$work/component/Bom"
(cd "$work/component" && xar --compression none -cf "$work/driver.pkg" PackageInfo Bom Payload Scripts)

cat "$project/LICENSE" - "$project/NOTICE" > "$work/resources/LICENSE.txt" <<SEPARATOR

--------

SEPARATOR
cp "$packaging/conclusion.html" "$work/resources/conclusion.html"
sed -e "s|@VERSION@|$version|g" -e "s|@PACKAGE_ID@|$package_id|g" "$packaging/distribution.xml.in" > "$work/distribution.xml"
productbuild --quiet --distribution "$work/distribution.xml" --resources "$work/resources" --package-path "$work" "$output"

echo "Built $output"
