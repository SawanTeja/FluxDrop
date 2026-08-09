#!/bin/bash
#
# build-appimage.sh — Build a portable FluxDrop.AppImage
#
# Run from the repo root:  bash linux/appimage/build-appimage.sh
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
APPDIR="${REPO_ROOT}/FluxDrop.AppDir"
APPIMAGETOOL="${REPO_ROOT}/appimagetool-x86_64.AppImage"

# ── Blacklisted libraries (provided by the base system) ───────
# These must NOT be bundled — they are tightly coupled to the kernel/GPU/display
BLACKLIST=(
    "linux-vdso.so"
    "ld-linux-x86-64.so"
    "libc.so"
    "libm.so"
    "libdl.so"
    "librt.so"
    "libpthread.so"
    "libstdc++.so"
    "libgcc_s.so"
    "libresolv.so"
    "libGL.so"
    "libGLX.so"
    "libGLdispatch.so"
    "libEGL.so"
    "libdrm.so"
    "libgbm.so"
    "libX11.so"
    "libxcb.so"
    "libX11-xcb.so"
    "libwayland-client.so"
    "libexpat.so"
    "libz.so"
    "libgmp.so"
    "libcom_err.so"
    "libfreetype.so"
    "libfontconfig.so"
    "libfribidi.so"
    "libharfbuzz.so"
)

is_blacklisted() {
    local lib_name="$1"
    for bl in "${BLACKLIST[@]}"; do
        if [[ "$lib_name" == "$bl"* ]]; then
            return 0
        fi
    done
    return 1
}

echo "==> [1/6] Building core library..."
mkdir -p "${REPO_ROOT}/build"
cmake -S "${REPO_ROOT}" -B "${REPO_ROOT}/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "${REPO_ROOT}/build" -j"$(nproc)"

echo "==> [2/6] Building GUI..."
mkdir -p "${REPO_ROOT}/linux/build"
cmake -S "${REPO_ROOT}/linux" -B "${REPO_ROOT}/linux/build" -DCMAKE_BUILD_TYPE=Release
cmake --build "${REPO_ROOT}/linux/build" -j"$(nproc)"

echo "==> [3/6] Preparing AppDir..."
rm -rf "${APPDIR}"
mkdir -p "${APPDIR}/usr/bin"
mkdir -p "${APPDIR}/usr/lib"
mkdir -p "${APPDIR}/usr/share/icons/hicolor/256x256/apps"
mkdir -p "${APPDIR}/usr/share/applications"
mkdir -p "${APPDIR}/usr/share/glib-2.0/schemas"

# Binary (unmodified — no patchelf)
cp "${REPO_ROOT}/linux/build/fluxdrop_gui" "${APPDIR}/usr/bin/"

# Desktop file & icon
cp "${REPO_ROOT}/linux/appimage/FluxDrop.desktop" "${APPDIR}/usr/share/applications/"
cp "${REPO_ROOT}/linux/appimage/FluxDrop.desktop" "${APPDIR}/"

# Install icons at multiple sizes, with both "fluxdroplogo" and app-id names
for SIZE in 48 128 256; do
    mkdir -p "${APPDIR}/usr/share/icons/hicolor/${SIZE}x${SIZE}/apps"
    convert "${REPO_ROOT}/linux/assets/fluxdroplogo.png" -resize "${SIZE}x${SIZE}" \
        "${APPDIR}/usr/share/icons/hicolor/${SIZE}x${SIZE}/apps/fluxdroplogo.png"
    # GNOME dock looks up icons by app-id (dev.fluxdrop.app)
    cp "${APPDIR}/usr/share/icons/hicolor/${SIZE}x${SIZE}/apps/fluxdroplogo.png" \
       "${APPDIR}/usr/share/icons/hicolor/${SIZE}x${SIZE}/apps/dev.fluxdrop.app.png"
done

cp "${APPDIR}/usr/share/icons/hicolor/256x256/apps/fluxdroplogo.png" "${APPDIR}/fluxdroplogo.png"
# .DirIcon is what file managers use to show the AppImage icon
ln -sf fluxdroplogo.png "${APPDIR}/.DirIcon"

# App assets — next to binary so relative "assets/" path works
cp -r "${REPO_ROOT}/linux/assets" "${APPDIR}/usr/bin/assets"

# AppRun
cp "${REPO_ROOT}/linux/appimage/AppRun" "${APPDIR}/AppRun"
chmod +x "${APPDIR}/AppRun"

echo "==> [4/6] Bundling shared libraries..."
# Collect all dependencies via ldd, skip blacklisted ones
ldd "${APPDIR}/usr/bin/fluxdrop_gui" | while read -r line; do
    # Extract the path from lines like: libgtk-4.so.1 => /lib64/libgtk-4.so.1 (0x...)
    lib_path="$(echo "$line" | awk '{print $3}')"
    lib_name="$(echo "$line" | awk '{print $1}')"

    # Skip non-path lines and blacklisted libs
    if [[ -z "$lib_path" || "$lib_path" == "not" || ! -f "$lib_path" ]]; then
        continue
    fi

    if is_blacklisted "$lib_name"; then
        echo "  SKIP (blacklisted): $lib_name"
        continue
    fi

    echo "  BUNDLE: $lib_name"
    cp -n "$lib_path" "${APPDIR}/usr/lib/" 2>/dev/null || true
done

# Also resolve transitive dependencies of all bundled libs
echo "  Resolving transitive dependencies..."
for bundled_lib in "${APPDIR}"/usr/lib/*.so*; do
    ldd "$bundled_lib" 2>/dev/null | while read -r line; do
        lib_path="$(echo "$line" | awk '{print $3}')"
        lib_name="$(echo "$line" | awk '{print $1}')"

        if [[ -z "$lib_path" || "$lib_path" == "not" || ! -f "$lib_path" ]]; then
            continue
        fi
        if is_blacklisted "$lib_name"; then
            continue
        fi
        cp -n "$lib_path" "${APPDIR}/usr/lib/" 2>/dev/null || true
    done
done

echo "==> [5/6] Bundling GTK4 runtime data..."
# GSettings schemas
cp /usr/share/glib-2.0/schemas/*.xml "${APPDIR}/usr/share/glib-2.0/schemas/" 2>/dev/null || true
glib-compile-schemas "${APPDIR}/usr/share/glib-2.0/schemas/"

# GDK pixbuf loaders
GDK_PIXBUF_DIR=$(pkg-config --variable=gdk_pixbuf_moduledir gdk-pixbuf-2.0 2>/dev/null || echo "/usr/lib64/gdk-pixbuf-2.0/2.10.0/loaders")
GDK_PIXBUF_BASE=$(dirname "$(dirname "$GDK_PIXBUF_DIR")")
if [ -d "$GDK_PIXBUF_BASE" ]; then
    mkdir -p "${APPDIR}/usr/lib/gdk-pixbuf-2.0"
    cp -r "$GDK_PIXBUF_BASE"/* "${APPDIR}/usr/lib/gdk-pixbuf-2.0/" 2>/dev/null || true
    # Fix paths in loaders.cache to be relative
    if [ -f "${APPDIR}/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache" ]; then
        sed -i "s|$GDK_PIXBUF_DIR|${APPDIR}/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders|g" \
            "${APPDIR}/usr/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache" || true
    fi
fi

# Adwaita icon theme (GTK4 needs at least the index)
if [ -d /usr/share/icons/Adwaita ]; then
    mkdir -p "${APPDIR}/usr/share/icons/Adwaita"
    cp -r /usr/share/icons/Adwaita/* "${APPDIR}/usr/share/icons/Adwaita/" 2>/dev/null || true
fi

echo "==> [6/6] Creating AppImage with appimagetool..."
if [ ! -f "${APPIMAGETOOL}" ]; then
    wget -q "https://github.com/AppImage/appimagetool/releases/download/continuous/appimagetool-x86_64.AppImage" \
         -O "${APPIMAGETOOL}"
    chmod +x "${APPIMAGETOOL}"
fi

cd "${REPO_ROOT}"
ARCH=x86_64 "${APPIMAGETOOL}" "${APPDIR}" "FluxDrop-x86_64.AppImage"

echo ""
echo "✅  Done!  Your AppImage is ready:"
ls -lh FluxDrop-x86_64.AppImage
echo ""
echo "Run it with:  ./FluxDrop-x86_64.AppImage"
