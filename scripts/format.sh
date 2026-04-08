#!/usr/bin/env bash

cd "$(dirname "$0")/.." || exit

if ! command -v clang-format &> /dev/null; then
    echo "Error: clang-format is not installed."
    exit 1
fi

echo "Formatting C++ code..."

find loom/ skewer/ libs/ \( -name "*.cc" -o -name "*.cpp" -o -name "*.h" \) -print0 | xargs -0 clang-format -i -style=file

echo "Done."
