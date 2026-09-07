#!/bin/bash
set -e

CMD=$1

if [ "$CMD" = "format" ]; then
    echo "Formatting C++ files..."
    find Engine Linux Windows Logger android/app/src/main/cpp -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) -exec clang-format -i {} +
    echo "Formatting complete."
elif [ "$CMD" = "check" ]; then
    echo "Linting C++ files..."
    if [ ! -f "compile_commands.json" ] && [ ! -f "Engine/build/compile_commands.json" ]; then
        echo "Warning: compile_commands.json not found. Run CMake with -DCMAKE_EXPORT_COMPILE_COMMANDS=ON first for best results."
    fi
    # Use xargs to handle large number of files and ignore errors from single files to continue
    find Engine Linux Windows Logger android/app/src/main/cpp -type f \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) | xargs -I {} clang-tidy {}
    echo "Linting complete."
else
    echo "Usage: $0 {format|check}"
    exit 1
fi
