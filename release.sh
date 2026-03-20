#!/usr/bin/env bash

# Build and publish a new release to GitHub.
# Usage: ./release.sh <version-tag>   e.g.  ./release.sh v1.2.3

set -e

TAG="${1:-}"
if [ -z "$TAG" ]; then
    echo "Usage: $0 <version-tag>   e.g. $0 v1.2.3"
    exit 1
fi

REPO="bbetter/linux-wallpaperengine"
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
BINARY="$BUILD_DIR/output/linux-wallpaperengine"
BUNDLE_ROOT="$BUILD_DIR/release"
BUNDLE_DIRNAME="linux-wallpaperengine-${TAG#v}-linux64"
BUNDLE_DIR="$BUNDLE_ROOT/$BUNDLE_DIRNAME"
BUNDLE_ARCHIVE_ZST="$BUILD_DIR/${BUNDLE_DIRNAME}.tar.zst"
BUNDLE_ARCHIVE_GZ="$BUILD_DIR/${BUNDLE_DIRNAME}.tar.gz"
LEGACY_BIN="$BUILD_DIR/${BUNDLE_DIRNAME}-linux-wallpaperengine"

echo "Building..."
cmake --build "$BUILD_DIR" --parallel

if [ ! -f "$BINARY" ]; then
    echo "Error: binary not found at $BINARY after build"
    exit 1
fi

echo "Packaging runtime bundle..."
rm -rf "$BUNDLE_DIR" "$BUNDLE_ARCHIVE_ZST" "$BUNDLE_ARCHIVE_GZ" "$LEGACY_BIN"
mkdir -p "$BUNDLE_DIR"

# Bundle the complete runtime directory required for web/application wallpapers:
# executable, shared libs, CEF resource files, locales, and helper binaries.
cp -a "$BUILD_DIR/output/." "$BUNDLE_DIR/"
echo "Stripping runtime binaries..."
if command -v strip >/dev/null 2>&1; then
    while IFS= read -r -d '' f; do
        strip --strip-unneeded "$f" 2>/dev/null || true
    done < <(find "$BUNDLE_DIR" -type f \( -name "*.so" -o -name "*.so.*" -o -name "linux-wallpaperengine" -o -name "chrome-sandbox" \) -print0)
fi

cp "$BUNDLE_DIR/linux-wallpaperengine" "$LEGACY_BIN"

if command -v zstd >/dev/null 2>&1; then
    echo "Compressing bundle with zstd..."
    tar -C "$BUNDLE_ROOT" --zstd -cf "$BUNDLE_ARCHIVE_ZST" "$BUNDLE_DIRNAME"
    BUNDLE_ASSET="$BUNDLE_ARCHIVE_ZST"
else
    echo "Compressing bundle with gzip (zstd not available)..."
    tar -C "$BUNDLE_ROOT" -czf "$BUNDLE_ARCHIVE_GZ" "$BUNDLE_DIRNAME"
    BUNDLE_ASSET="$BUNDLE_ARCHIVE_GZ"
fi

if [ ! -f "$BUNDLE_ASSET" ]; then
    echo "Error: bundle archive was not created: $BUNDLE_ASSET"
    exit 1
fi

if [ ! -f "$LEGACY_BIN" ]; then
    echo "Error: legacy binary asset was not created: $LEGACY_BIN"
    exit 1
fi

echo "Creating GitHub release $TAG..."
gh release create "$TAG" \
    "$BUNDLE_ASSET" \
    "$LEGACY_BIN#linux-wallpaperengine" \
    --repo "$REPO" \
    --title "$TAG" \
    --generate-notes

echo ""
echo "Done. Release $TAG published to github.com/$REPO/releases/tag/$TAG"
