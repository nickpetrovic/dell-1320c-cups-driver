#!/bin/sh
set -eu

project=$(cd "$(dirname "$0")/../.." && pwd)
packaging=$project/packaging/linux
version=${VERSION:-$(cat "$project/VERSION")}
architectures=${ARCHITECTURES:-amd64 arm64}
build_image=${BUILD_IMAGE:-almalinux:9}
nfpm_image=${NFPM_IMAGE:-goreleaser/nfpm:v2.48.0}
build=${BUILD:-build}
case $build in
    /*) ;;
    *) build=$project/$build ;;
esac
dist=$build/dist
host_ids="$(id -u):$(id -g)"
maintainer='Nick Petrovic <4001122+nickpetrovic@users.noreply.github.com>'
summary='CUPS printer driver for the Dell Color Laser 1320c'
details='A native CUPS filter and PPD for the Dell Color Laser 1320c, which is the
Fuji Xerox DocuPrint C525 A sold under the Dell name, with the dell1320c
command for creating print queues.'

mkdir -p "$dist"
for architecture in $architectures; do
    case $architecture in
        amd64) machine=x86_64 ;;
        arm64) machine=aarch64 ;;
        *) echo "unsupported architecture $architecture (use amd64 or arm64)" >&2; exit 1 ;;
    esac
    work=$build/linux-$architecture
    rm -rf "$work"
    mkdir -p "$work"

    docker run --rm --platform "linux/$architecture" -v "$project:/source:ro" -v "$work:/output" -e HOST_IDS="$host_ids" "$build_image" sh -euc '
        dnf install -y -q gcc-toolset-14-gcc-c++ make cups-devel zlib-devel >/dev/null
        . /opt/rh/gcc-toolset-14/enable
        mkdir /tmp/source
        tar -C /source --exclude ./build -cf - . | tar -C /tmp/source -xf -
        cd /tmp/source
        make WERROR=1 STATIC_RUNTIME=1 PREFIX=/usr >/dev/null
        make WERROR=1 STATIC_RUNTIME=1 PREFIX=/usr DESTDIR=/output/root install >/dev/null
        strip /output/root/usr/lib/cups/filter/rastertodell1320c
        gzip -9 -n /output/root/usr/share/man/man1/dell1320c.1
        tar --owner=0 --group=0 --numeric-owner -C /output/root -czf /output/dell1320c.tar.gz usr
        chown -R "$HOST_IDS" /output
    '

    cp "$project/LICENSE" "$packaging/copyright" "$packaging/postinstall.sh" "$packaging/nfpm.yaml" "$work/"
    cat > "$work/changelog.yml" <<CHANGELOG
- semver: $version-1
  date: $(date -u +%Y-%m-%dT%H:%M:%SZ)
  packager: $maintainer
  deb:
    urgency: medium
    distributions:
      - unstable
  changes:
    - note: Release $version.
CHANGELOG
    for packager in deb rpm; do
        if [ "$packager" = deb ]; then
            description="$summary
$details"
        else
            description=$details
        fi
        docker run --rm --user "$host_ids" -v "$work:/work" -v "$dist:/dist" -w /work -e ARCH="$architecture" -e VERSION="$version" \
            -e PACKAGE_DESCRIPTION="$description" -e MAINTAINER="$maintainer" "$nfpm_image" package -f nfpm.yaml -p "$packager" -t /dist/ >/dev/null
    done
    mv "$work/dell1320c.tar.gz" "$dist/dell1320c-$version-linux-$machine.tar.gz"
done

ls -1 "$dist"
