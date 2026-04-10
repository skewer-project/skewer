#!/usr/bin/env bash

cd "$(dirname "$0")/.." || exit

# Install clang-format locally if not present
CLANG_FMT_DIR="./scripts/clang-format"
if [ ! -d "$CLANG_FMT_DIR" ]; then
    echo "Installing clang-format..."
    python3 -m pip install --upgrade pip --target "$CLANG_FMT_DIR" "clang-format>=21"
fi

echo "Formatting C++ code..."

# Run clang-format via Python module
PYTHONPATH="$CLANG_FMT_DIR" python3 - <<'PYTHON_SCRIPT'
import sys
import subprocess

# Find files
result = subprocess.run([
    'find', 'skewer/apps', 'skewer/src', 'skewer/tests',
    'loom/src', 'loom/tests',
    'libs/exrio/src', 'libs/exrio/tests', 'libs/exrio/include',
    '-type', 'f', '(', '-name', '*.cc', '-o', '-name', '*.cpp', '-o', '-name', '*.h', ')'
], capture_output=True, text=True)

files = [f for f in result.stdout.strip().split('\n') if f]
if not files:
    print('No C/C++ files found.')
    sys.exit(0)

print(f'Formatting {len(files)} files...')

from clang_format import clang_format
sys.argv = ['clang-format', '-i', '-style=file'] + files
sys.exit(clang_format())
PYTHON_SCRIPT

echo "Done."