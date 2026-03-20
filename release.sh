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
BUNDLE_ARCHIVE="$BUILD_DIR/${BUNDLE_DIRNAME}.tar.gz"

echo "Building..."
cmake --build "$BUILD_DIR" --parallel

if [ ! -f "$BINARY" ]; then
    echo "Error: binary not found at $BINARY after build"
    exit 1
fi

echo "Packaging runtime bundle..."
rm -rf "$BUNDLE_DIR" "$BUNDLE_ARCHIVE"
mkdir -p "$BUNDLE_DIR"

# Bundle the complete runtime directory required for web/application wallpapers:
# executable, shared libs, CEF resource files, locales, and helper binaries.
cp -a "$BUILD_DIR/output/." "$BUNDLE_DIR/"
tar -C "$BUNDLE_ROOT" -czf "$BUNDLE_ARCHIVE" "$BUNDLE_DIRNAME"

if [ ! -f "$BUNDLE_ARCHIVE" ]; then
    echo "Error: bundle archive was not created: $BUNDLE_ARCHIVE"
    exit 1
fi

echo "Creating GitHub release $TAG..."
gh release create "$TAG" \
    "$BUNDLE_ARCHIVE" \
    "$BINARY" \
    --repo "$REPO" \
    --title "$TAG" \
    --generate-notes

echo ""
echo "Done. Release $TAG published to github.com/$REPO/releases/tag/$TAG"
