#!/usr/bin/env bash

# Runs clang-tidy on the codebase using compile_commands.json

# Use build/tidy as the default build directory to keep the root clean.
# This aligns with the 'tidy' preset in CMakePresets.json and is ignored by git.
BUILD_DIR="build/tidy"
TIDY_FLAGS=""
THREADS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)

# Parse arguments using a while loop for better reliability
while [[ $# -gt 0 ]]; do
    case $1 in
        --fix)
            TIDY_FLAGS="$TIDY_FLAGS -fix"
            shift
            ;;
        --threads)
            THREADS=$2
            shift 2
            ;;
        *)
            shift
            ;;
    esac
done

# macOS specific: Add Homebrew LLVM to path if it exists
if [[ "$OSTYPE" == "darwin"* ]]; then
    LLVM_PATH="/opt/homebrew/opt/llvm/bin"
    if [ -d "$LLVM_PATH" ]; then
        export PATH="$LLVM_PATH:$PATH"
    fi
fi

# Configure using the 'tidy' preset if it hasn't been configured yet.
if [ ! -d "$BUILD_DIR" ]; then
    echo "Configuring CMake using 'tidy' preset in $BUILD_DIR..."
    cmake --preset tidy
fi

# Regex to filter files (adjust to match your project structure)
# Matches files in apps/, skewer/src/, loom/src/, and libs/exrio/src/
FILE_REGEX="(apps|skewer/src|loom/src|libs/exrio/src)/.*"

# Use run-clang-tidy if available, otherwise fallback to finding files
if command -v run-clang-tidy &> /dev/null; then
    echo "Running: run-clang-tidy -p $BUILD_DIR -j $THREADS $TIDY_FLAGS \"$FILE_REGEX\""
    run-clang-tidy -p "$BUILD_DIR" -j "$THREADS" $TIDY_FLAGS "$FILE_REGEX"
elif command -v run-clang-tidy.py &> /dev/null; then
    echo "Running: run-clang-tidy.py -p $BUILD_DIR -j $THREADS $TIDY_FLAGS \"$FILE_REGEX\""
    run-clang-tidy.py -p "$BUILD_DIR" -j "$THREADS" $TIDY_FLAGS "$FILE_REGEX"
else
    echo "run-clang-tidy not found. Running clang-tidy manually on files..."
    echo "Using flags: $TIDY_FLAGS"
    # Find files and run clang-tidy in parallel using xargs
    find apps skewer/src loom/src libs/exrio/src -name "*.cc" -o -name "*.cpp" -o -name "*.h" | xargs -P "$THREADS" -I {} clang-tidy -p "$BUILD_DIR" $TIDY_FLAGS {}
fi
