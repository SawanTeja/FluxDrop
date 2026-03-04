#!/bin/sh
# deploy.sh
# Run this inside the build directory from MSYS2 terminal
# to copy all required MinGW64 DLLs alongside the executable.

EXECUTABLE="fluxdrop_gui.exe"

if [ ! -f "$EXECUTABLE" ]; then
    echo "Error: $EXECUTABLE not found in the current directory."
    exit 1
fi

echo "Gathering MSYS2 MinGW64 dependencies for $EXECUTABLE..."

# Find all DLLs that belong to /mingw64/bin/ and copy them over
ldd "$EXECUTABLE" | grep '/mingw64/bin/' | awk '{print $3}' | while read -r dll_path; do
    echo "Copying $dll_path"
    cp "$dll_path" .
done

echo "Setting up GTK runtime data..."
mkdir -p share/glib-2.0/schemas
mkdir -p share/icons
mkdir -p lib/gdk-pixbuf-2.0/2.10.0/loaders

# Copy default GTK/GLib schemas
cp /mingw64/share/glib-2.0/schemas/* share/glib-2.0/schemas/ 2>/dev/null
# Compile schemas
glib-compile-schemas share/glib-2.0/schemas/

# Copy Adwaita and hicolor icons (needed for GTK4)
cp -r /mingw64/share/icons/Adwaita share/icons/ 2>/dev/null
cp -r /mingw64/share/icons/hicolor share/icons/ 2>/dev/null

# Copy loaders cache for images
cp /mingw64/lib/gdk-pixbuf-2.0/2.10.0/loaders.cache lib/gdk-pixbuf-2.0/2.10.0/ 2>/dev/null
cp -r /mingw64/lib/gdk-pixbuf-2.0/2.10.0/loaders/* lib/gdk-pixbuf-2.0/2.10.0/loaders/ 2>/dev/null

echo "Done! You can now run $EXECUTABLE standalone on this machine."
