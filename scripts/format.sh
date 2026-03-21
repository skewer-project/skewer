#!/usr/bin/env bash

# Grabs the directory this script is in, then goes to the root
cd "$(dirname "$0")/.." || exit

# Check if clang-format is installed
if ! command -v clang-format &> /dev/null; then
    echo "Error: clang-format is not installed."
    exit 1
fi

echo "Formatting all Skewer project code..."

# Find all C/C++ files in the relevant directories
find     skewer/apps     skewer/src     skewer/tests     loom/apps     loom/src     loom/tests     libs/exrio/src     libs/exrio/tests     libs/exrio/include     -type f \( -name "*.cc" -o -name "*.cpp" -o -name "*.h" \) -print0 | xargs -0 clang-format -i -style=file

echo "Done."
