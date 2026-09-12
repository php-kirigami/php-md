#!/bin/bash
set -e
BUILD=/mnt/c/projects/kirigami/php-md/vendor/build/src/build
SRC=/mnt/c/projects/kirigami/php-md/vendor/build/src
DEST=/mnt/c/projects/kirigami/php-md/vendor/libcmark-gfm

mkdir -p "$DEST/include" "$DEST/lib"

cp "$BUILD/src/libcmark-gfm.a" "$DEST/lib/"
cp "$BUILD/extensions/libcmark-gfm-extensions.a" "$DEST/lib/"

cp "$SRC/src/cmark-gfm.h" "$DEST/include/"
cp "$SRC/src/cmark-gfm-extension_api.h" "$DEST/include/"
cp "$BUILD/src/cmark-gfm_export.h" "$DEST/include/"
cp "$BUILD/src/cmark-gfm_version.h" "$DEST/include/"
cp "$SRC/extensions/cmark-gfm-core-extensions.h" "$DEST/include/"

echo "--- staged ---"
find "$DEST" -type f
