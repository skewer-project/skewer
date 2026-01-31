#!/usr/bin/env bash

find src -name '*.cpp' -o -name '*.h' | xargs clang-format -i -style=file

# Grabs the directory this script is in, then goes up one level
cd "$(dirname "$0")/.." || exit

# Check if clang-format is installed
if ! command -v clang-format &> /dev/null; then
    echo "Error: clang-format is not installed."
    exit 1
fi

echo "Formatting code..."

# uses parentheses \( \) to group extensions correctly
# uses -print0 and -0 to handle filenames with spaces safely
find src \( -name '*.cpp' -o -name '*.h' \) -print0 | xargs -0 clang-format -i -style=file

echo "Done."
