#!/bin/sh
# Bundle MSYS2 runtime files + Qt6 plugins for the FluxDrop Windows executable.
# Run from MSYS2 UCRT64 or MinGW64 terminal:
#   bash deploy.sh [path/to/fluxdrop_gui.exe]

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

    if [ -f "${SCRIPT_DIR}/build/${target}" ]; then
        printf '%s\n' "${SCRIPT_DIR}/build/${target}"
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

bundle_qt_plugins() {
    out_dir="$1"
    qt_plugin_path=""

    if command -v qmake6 >/dev/null 2>&1; then
        qt_plugin_path=$(qmake6 -query QT_INSTALL_PLUGINS 2>/dev/null || true)
    fi

    if [ -z "$qt_plugin_path" ] || [ ! -d "$qt_plugin_path" ]; then
        qt_plugin_path="${MINGW_ROOT}/share/qt6/plugins"
    fi

    if [ ! -d "$qt_plugin_path" ]; then
        echo "  WARNING: Could not find Qt6 plugins directory"
        return
    fi

    echo "  Copying Qt6 plugins from ${qt_plugin_path}..."

    # Platform plugin (required!)
    if [ -d "${qt_plugin_path}/platforms" ]; then
        mkdir -p "${out_dir}/platforms"
        cp -f "${qt_plugin_path}/platforms/qwindows.dll" "${out_dir}/platforms/" 2>/dev/null || true
        echo "    platforms/qwindows.dll"
    fi

    # Style plugins
    if [ -d "${qt_plugin_path}/styles" ]; then
        mkdir -p "${out_dir}/styles"
        cp -f "${qt_plugin_path}/styles/"*.dll "${out_dir}/styles/" 2>/dev/null || true
        echo "    styles/"
    fi

    # Image format plugins
    if [ -d "${qt_plugin_path}/imageformats" ]; then
        mkdir -p "${out_dir}/imageformats"
        cp -f "${qt_plugin_path}/imageformats/"*.dll "${out_dir}/imageformats/" 2>/dev/null || true
        echo "    imageformats/"
    fi

    # Icon engine plugins
    if [ -d "${qt_plugin_path}/iconengines" ]; then
        mkdir -p "${out_dir}/iconengines"
        cp -f "${qt_plugin_path}/iconengines/"*.dll "${out_dir}/iconengines/" 2>/dev/null || true
        echo "    iconengines/"
    fi
}

bundle_executable() {
    exe_path="$1"
    out_dir="${SCRIPT_DIR}/dist"
    exe_name=$(basename "$exe_path")

    echo "Bundling runtime files for ${exe_name}..."

    # Create dist directory
    rm -rf "$out_dir"
    mkdir -p "$out_dir"

    # Copy executable
    cp -f "$exe_path" "$out_dir/"
    echo "  Copied ${exe_name}"

    # Copy MinGW DLLs
    list_mingw_dlls "$exe_path" | while IFS= read -r dll_path; do
        echo "  Copying $(basename "$dll_path")"
        cp -f "$dll_path" "$out_dir/"
    done

    # Also resolve transitive dependencies of bundled DLLs
    echo "  Resolving transitive dependencies..."
    for bundled_dll in "${out_dir}"/*.dll; do
        [ -f "$bundled_dll" ] || continue
        list_mingw_dlls "$bundled_dll" | while IFS= read -r dll_path; do
            dll_name=$(basename "$dll_path")
            if [ ! -f "${out_dir}/${dll_name}" ]; then
                echo "    + ${dll_name}"
                cp -f "$dll_path" "$out_dir/"
            fi
        done
    done

    # Bundle Qt6 plugins
    bundle_qt_plugins "$out_dir"

    # Copy assets
    if [ -d "${SCRIPT_DIR}/assets" ]; then
        echo "  Copying assets..."
        cp -r "${SCRIPT_DIR}/assets" "${out_dir}/assets"
    fi

    echo ""
    echo "Done! Standalone package created in: ${out_dir}"
    echo "Run it with: ${out_dir}/${exe_name}"
}

# ── Main ──────────────────────────────────────────────────────

MINGW_ROOT=$(detect_mingw_root)
MINGW_BIN="${MINGW_ROOT}/bin"

echo "Using MSYS2 runtime from ${MINGW_ROOT}"

if [ "$#" -eq 0 ]; then
    exe_path=$(resolve_executable "fluxdrop_gui.exe")
else
    exe_path=$(resolve_executable "$1")
fi

bundle_executable "$exe_path"
