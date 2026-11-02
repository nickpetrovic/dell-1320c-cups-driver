#!/bin/sh
set -eu

project=$(cd "$(dirname "$0")/../.." && pwd)
packages=$(cd "${1:-$project/build/dist}" && pwd)
deb_images=${DEB_IMAGES:-debian:bookworm debian:trixie ubuntu:22.04 ubuntu:24.04 ubuntu:26.04}
rpm_images=${RPM_IMAGES:-almalinux:9 fedora:latest}
tarball_images=${TARBALL_IMAGES:-debian:trixie}
runs=0
failures=0

test_package() {
    file=$1
    platform=$2
    images=$3
    for image in $images; do
        printf '\n== %s on %s\n' "$file" "$image"
        runs=$((runs + 1))
        if ! docker run --rm --platform "$platform" -v "$packages:/packages:ro" -v "$project/test:/test:ro" "$image" \
            sh /test/packages/print-test.sh "/packages/$file" /test/documents/multi.pdf 3; then
            failures=$((failures + 1))
        fi
    done
}

for path in "$packages"/*; do
    file=$(basename "$path")
    case $file in
        *_amd64.deb) test_package "$file" linux/amd64 "$deb_images" ;;
        *_arm64.deb) test_package "$file" linux/arm64 "$deb_images" ;;
        *.x86_64.rpm) test_package "$file" linux/amd64 "$rpm_images" ;;
        *.aarch64.rpm) test_package "$file" linux/arm64 "$rpm_images" ;;
        *-linux-x86_64.tar.gz) test_package "$file" linux/amd64 "$tarball_images" ;;
        *-linux-aarch64.tar.gz) test_package "$file" linux/arm64 "$tarball_images" ;;
    esac
done

if [ "$runs" -eq 0 ]; then
    echo "no Linux packages in $packages; build them with make linux-packages" >&2
    exit 1
fi
printf '\n%d of %d package tests passed\n' "$((runs - failures))" "$runs"
[ "$failures" -eq 0 ]
