#!/usr/bin/env bash

cd "$(dirname "$0")/.." || exit

# Try uvx first, then pipx, then fall back to manual install
if command -v uv &> /dev/null; then
    CLANG_FMT="uvx clang-format>=21"
    echo "Using uvx for clang-format..."
elif command -v pipx &> /dev/null; then
    CLANG_FMT="pipx run clang-format>=21"
    echo "Using pipx for clang-format..."
else
    # Fall back to manual install
    CLANG_FMT_DIR="./scripts/clang-format"
    if [ ! -d "$CLANG_FMT_DIR" ]; then
        echo "Installing clang-format locally..."
        python3 -m pip install --upgrade pip --target "$CLANG_FMT_DIR" "clang-format>=21"
    fi
    CLANG_FMT="$CLANG_FMT_DIR/bin/clang-format"
    echo "Using local clang-format..."
fi

echo "Formatting C++ code..."

files=()
while IFS= read -r -d '' file; do
    files+=("$file")
done < <(find skewer/apps skewer/src skewer/tests loom/src loom/tests libs/exrio/src libs/exrio/tests libs/exrio/include -type f \( -name "*.cc" -o -name "*.cpp" -o -name "*.h" \) -print0)

if [ ${#files[@]} -eq 0 ]; then
    echo "No C/C++ files found."
    exit 0
fi

$CLANG_FMT -i -style=file "${files[@]}"

echo "Done."