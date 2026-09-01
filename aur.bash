#!/usr/bin/env bash
# Generates the AUR submission files (PKGBUILD + .SRCINFO) into ./aur/,
# based on the release tag matching src/version.h's REPOQ_VERSION.
#
# Usage: ./aur.bash
#
# The generated files are NOT committed anywhere by this script. Clone the
# AUR repo separately, copy aur/PKGBUILD and aur/.SRCINFO over it, review,
# and commit/push from there manually.
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
AUR_DIR="$REPO_ROOT/aur"
VERSION_HEADER="$REPO_ROOT/src/version.h"

pkgname=repoq
pkgrel=1
pkgurl="https://github.com/pysio2007/repoq"

command -v makepkg >/dev/null 2>&1 || { echo "error: makepkg not found (install pacman/base-devel)" >&2; exit 1; }
command -v curl >/dev/null 2>&1 || { echo "error: curl not found" >&2; exit 1; }

pkgver=$(grep -oP '(?<=REPOQ_VERSION ")[^"]+' "$VERSION_HEADER")
if [[ -z "$pkgver" ]]; then
    echo "error: could not read REPOQ_VERSION from $VERSION_HEADER" >&2
    exit 1
fi

# NOT GitHub's auto-generated /archive/refs/tags/ archive: that endpoint is
# not guaranteed byte-stable across regenerations (observed 3 different
# sha256 sums for the same tag within an hour). Use the release asset
# built once by .github/workflows/release.yml via `git archive` instead,
# which is a fixed blob GitHub will never regenerate.
tarball_url="$pkgurl/releases/download/v${pkgver}/${pkgname}-${pkgver}.tar.gz"

echo "==> Packaging $pkgname $pkgver"
echo "==> Fetching source tarball to compute checksum: $tarball_url"

tmpfile=$(mktemp)
trap 'rm -f "$tmpfile"' EXIT

if ! curl -fsSL "$tarball_url" -o "$tmpfile"; then
    echo "error: failed to download $tarball_url" >&2
    echo "       has the release workflow finished for tag v${pkgver}?" >&2
    echo "       (.github/workflows/release.yml, triggered by pushing that tag)" >&2
    exit 1
fi

sha256=$(sha256sum "$tmpfile" | awk '{print $1}')

mkdir -p "$AUR_DIR"

cat > "$AUR_DIR/PKGBUILD" <<EOF
# Maintainer: pysio <me@lilithya.su>
pkgname=$pkgname
pkgver=$pkgver
pkgrel=$pkgrel
pkgdesc="A lightweight command-line client for the Repology API"
arch=('x86_64' 'aarch64')
url="$pkgurl"
license=('Apache-2.0')
depends=('curl')
source=("\$pkgname-\$pkgver.tar.gz::$pkgurl/releases/download/v\$pkgver/\$pkgname-\$pkgver.tar.gz")
sha256sums=('$sha256')

build() {
    cd "\$pkgname-\$pkgver"
    make
}

package() {
    cd "\$pkgname-\$pkgver"
    install -Dm755 repoq "\$pkgdir/usr/bin/repoq"
    install -Dm644 LICENSE "\$pkgdir/usr/share/licenses/\$pkgname/LICENSE"
    install -Dm644 README.md "\$pkgdir/usr/share/doc/\$pkgname/README.md"
}
EOF

echo "==> Generating .SRCINFO"
(cd "$AUR_DIR" && makepkg --printsrcinfo > .SRCINFO)

echo "==> Done. Files written to $AUR_DIR:"
ls -1 "$AUR_DIR"
echo "==> Clone the AUR repo elsewhere, copy these files over it, review, and commit/push manually."
