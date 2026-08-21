#!/bin/bash
#
# bundle-gs.sh — Collect Ghostscript binary and its dylib dependencies
# into the plugin bundle for self-contained distribution.
#
# Usage: ./bundle-gs.sh <bundle_path> [gs_prefix]
#   bundle_path: path to PDFA.bundle
#   gs_prefix:   Ghostscript install prefix (default: brew --prefix ghostscript)
#
set -euo pipefail

BUNDLE="${1:?Usage: $0 <bundle_path> [gs_prefix]}"
GS_PREFIX="${2:-$(brew --prefix ghostscript)}"

HELPERS_DIR="$BUNDLE/Contents/Helpers"
FRAMEWORKS_DIR="$BUNDLE/Contents/Frameworks"
RESOURCES_DIR="$BUNDLE/Contents/Resources"

mkdir -p "$HELPERS_DIR" "$FRAMEWORKS_DIR"

echo "=== Bundling Ghostscript from $GS_PREFIX ==="

# 1. Copy gs binary
GS_BIN="$GS_PREFIX/bin/gs"
if [ ! -f "$GS_BIN" ]; then
    echo "ERROR: gs binary not found at $GS_BIN"
    exit 1
fi
cp "$GS_BIN" "$HELPERS_DIR/gs"
chmod 755 "$HELPERS_DIR/gs"

# 2. Collect all dylib dependencies (non-system)
collect_dylibs() {
    local binary="$1"
    otool -L "$binary" | tail -n +2 | awk '{print $1}' | while read -r lib; do
        # Skip system libraries
        case "$lib" in
            /usr/lib/*|/System/*) continue ;;
        esac
        echo "$lib"
    done
}

# Recursively collect all needed dylibs
copy_dylib() {
    local lib_path="$1"
    local lib_name
    lib_name=$(basename "$lib_path")

    # Skip if already processed
    if [ -f "$FRAMEWORKS_DIR/$lib_name" ]; then
        return
    fi

    # Resolve symlinks
    local real_path
    real_path=$(realpath "$lib_path" 2>/dev/null || echo "$lib_path")

    if [ ! -f "$real_path" ]; then
        echo "  WARNING: cannot find $lib_path"
        return
    fi

    echo "  Copying: $lib_name"
    cp "$real_path" "$FRAMEWORKS_DIR/$lib_name"
    chmod 644 "$FRAMEWORKS_DIR/$lib_name"

    # Fix its install name
    install_name_tool -id "@loader_path/../Frameworks/$lib_name" "$FRAMEWORKS_DIR/$lib_name" 2>/dev/null || true

    # Recursively collect its dependencies
    for dep in $(collect_dylibs "$FRAMEWORKS_DIR/$lib_name"); do
        local dep_name
        dep_name=$(basename "$dep")
        # Fix reference in this lib
        install_name_tool -change "$dep" "@loader_path/$dep_name" "$FRAMEWORKS_DIR/$lib_name" 2>/dev/null || true
        # Recurse
        copy_dylib "$dep"
    done
}

# Collect dependencies of gs binary
echo "--- Collecting dylibs for gs ---"
for lib in $(collect_dylibs "$HELPERS_DIR/gs"); do
    lib_name=$(basename "$lib")
    # Fix reference in gs binary
    install_name_tool -change "$lib" "@loader_path/../Frameworks/$lib_name" "$HELPERS_DIR/gs" 2>/dev/null || true
    copy_dylib "$lib"
done

# 3. Handle @rpath dependencies (e.g. libsharpyuv)
echo "--- Fixing @rpath references ---"
for fw in "$FRAMEWORKS_DIR"/*.dylib; do
    otool -L "$fw" 2>/dev/null | tail -n +2 | awk '{print $1}' | while read -r ref; do
        case "$ref" in
            @rpath/*)
                rpath_name=$(basename "$ref")
                # Try to find the library in common brew locations
                found=""
                for search in /opt/homebrew/lib /usr/local/lib; do
                    if [ -f "$search/$rpath_name" ]; then
                        found="$search/$rpath_name"
                        break
                    fi
                done
                if [ -n "$found" ] && [ ! -f "$FRAMEWORKS_DIR/$rpath_name" ]; then
                    echo "  Resolving @rpath: $rpath_name from $found"
                    cp "$found" "$FRAMEWORKS_DIR/$rpath_name"
                    chmod 644 "$FRAMEWORKS_DIR/$rpath_name"
                    install_name_tool -id "@loader_path/$rpath_name" "$FRAMEWORKS_DIR/$rpath_name" 2>/dev/null || true
                fi
                install_name_tool -change "$ref" "@loader_path/$rpath_name" "$fw" 2>/dev/null || true
                ;;
        esac
    done
done

# 4. Ad-hoc re-sign everything (install_name_tool invalidates code signatures)
echo "--- Re-signing binaries ---"
for fw in "$FRAMEWORKS_DIR"/*.dylib; do
    codesign --force --sign - "$fw" 2>/dev/null || true
done
codesign --force --sign - "$HELPERS_DIR/gs" 2>/dev/null || true

# 5. Copy Ghostscript resource files (fonts, ICC profiles, init scripts)
GS_SHARE="$GS_PREFIX/share/ghostscript"
GS_VERSION=$(ls "$GS_SHARE" | grep -E '^[0-9]' | head -1)
if [ -n "$GS_VERSION" ]; then
    GS_RES="$GS_SHARE/$GS_VERSION"
    echo "--- Copying Ghostscript resources ($GS_VERSION) ---"
    # Copy lib/ (PostScript init files - essential for gs to run)
    if [ -d "$GS_RES/lib" ]; then
        cp -R "$GS_RES/lib" "$RESOURCES_DIR/gs_lib"
    fi
    # Copy Resource/Init if exists
    if [ -d "$GS_RES/Resource" ]; then
        cp -R "$GS_RES/Resource" "$RESOURCES_DIR/gs_Resource"
    fi
    # Copy iccprofiles if exists
    if [ -d "$GS_RES/iccprofiles" ]; then
        cp -R "$GS_RES/iccprofiles" "$RESOURCES_DIR/gs_iccprofiles"
    fi
fi

echo "=== Done. Bundle contents: ==="
echo "Helpers:"
ls -la "$HELPERS_DIR"
echo "Frameworks:"
ls -la "$FRAMEWORKS_DIR"
echo ""
echo "Verify gs can find its libs:"
otool -L "$HELPERS_DIR/gs" | head -5
