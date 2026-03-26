#!/bin/sh
# Bundle MSYS2 runtime files for one or more Windows executables.
# Works with MinGW32, MinGW64, UCRT64, and similar MSYS2 MinGW shells.

set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "${SCRIPT_DIR}/.." && pwd)

detect_mingw_root() {
    if [ -n "${MINGW_PREFIX:-}" ] && [ -d "${MINGW_PREFIX}/bin" ]; then
        printf '%s\n' "$MINGW_PREFIX"
        return 0
    fi

    compiler_path=""
    if command -v gcc >/dev/null 2>&1; then
        compiler_path=$(command -v gcc)
    elif command -v cc >/dev/null 2>&1; then
        compiler_path=$(command -v cc)
    fi

    if [ -n "$compiler_path" ]; then
        printf '%s\n' "$(dirname "$(dirname "$compiler_path")")"
        return 0
    fi

    echo "Error: could not detect the active MSYS2 MinGW toolchain." >&2
    exit 1
}

resolve_executable() {
    target="$1"

    if [ -f "$target" ]; then
        printf '%s\n' "$target"
        return 0
    fi

    if [ -f "${REPO_ROOT}/$target" ]; then
        printf '%s\n' "${REPO_ROOT}/$target"
        return 0
    fi

    echo "Error: executable not found: $target" >&2
    exit 1
}

list_mingw_dlls() {
    exe_path="$1"

    ldd "$exe_path" 2>/dev/null | while IFS= read -r line; do
        set -- $line
        dep_path=""

        case "$line" in
            *" => "*)
                dep_path="${3:-}"
                ;;
            /*.dll*)
                dep_path="${1:-}"
                ;;
        esac

        case "$dep_path" in
            "${MINGW_BIN}"/*.dll)
                printf '%s\n' "$dep_path"
                ;;
        esac
    done | sort -u
}

bundle_gtk_runtime() {
    out_dir="$1"
    schema_dir="${out_dir}/share/glib-2.0/schemas"
    icons_dir="${out_dir}/share/icons"
    pixbuf_root="${out_dir}/lib/gdk-pixbuf-2.0"
    pixbuf_moduledir=$(pkg-config --variable=gdk_pixbuf_moduledir gdk-pixbuf-2.0 2>/dev/null || true)

    echo "  Copying GTK runtime data..."
    mkdir -p "$schema_dir"
    mkdir -p "$icons_dir"

    cp "${MINGW_SHARE}/glib-2.0/schemas/"* "$schema_dir/" 2>/dev/null || true
    if command -v glib-compile-schemas >/dev/null 2>&1; then
        glib-compile-schemas "$schema_dir" >/dev/null 2>&1 || true
    fi

    cp -r "${MINGW_SHARE}/icons/Adwaita" "$icons_dir/" 2>/dev/null || true
    cp -r "${MINGW_SHARE}/icons/hicolor" "$icons_dir/" 2>/dev/null || true

    if [ -n "$pixbuf_moduledir" ] && [ -d "$pixbuf_moduledir" ]; then
        pixbuf_base=$(dirname "$(dirname "$pixbuf_moduledir")")
        mkdir -p "$pixbuf_root"
        cp -r "${pixbuf_base}/." "$pixbuf_root/" 2>/dev/null || true
    fi
}

bundle_executable() {
    exe_path="$1"
    out_dir=$(CDPATH= cd -- "$(dirname -- "$exe_path")" && pwd)
    exe_name=$(basename "$exe_path")
    needs_gtk=0

    echo "Bundling runtime files for ${exe_path}..."

    if list_mingw_dlls "$exe_path" | grep -q 'libgtk-4'; then
        needs_gtk=1
    fi

    list_mingw_dlls "$exe_path" | while IFS= read -r dll_path; do
        echo "  Copying $(basename "$dll_path")"
        cp -f "$dll_path" "$out_dir/"
    done

    if [ "$needs_gtk" -eq 1 ]; then
        bundle_gtk_runtime "$out_dir"
        echo "  GTK bundle ready next to ${exe_name} (keep the generated share/ and lib/ folders with it)"
    fi
}

MINGW_ROOT=$(detect_mingw_root)
MINGW_BIN="${MINGW_ROOT}/bin"
MINGW_SHARE="${MINGW_ROOT}/share"

if [ "$#" -eq 0 ]; then
    set -- \
        "${REPO_ROOT}/build/fluxdrop.exe" \
        "${REPO_ROOT}/Win-Linux/build/fluxdrop_gui.exe"
fi

echo "Using MSYS2 runtime from ${MINGW_ROOT}"

for target in "$@"; do
    exe_path=$(resolve_executable "$target")
    bundle_executable "$exe_path"
done

echo "Done. The selected executables now have their MinGW DLLs copied beside them."
