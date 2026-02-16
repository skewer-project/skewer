#!/bin/bash
# scripts/tidy.sh
# Runs clang-tidy on the codebase using compile_commands.json

BUILD_DIR="build_tidy"
TIDY_FLAGS=""

# Parse arguments using a while loop for better reliability
while [[ $# -gt 0 ]]; do
    case $1 in
        --fix)
            TIDY_FLAGS="$TIDY_FLAGS -fix"
            shift
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

if [ ! -d "$BUILD_DIR" ]; then
    echo "Configuring CMake in $BUILD_DIR..."
    cmake -B "$BUILD_DIR" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON
fi

# Use run-clang-tidy if available, otherwise fallback to finding files
if command -v run-clang-tidy &> /dev/null; then
    echo "Running: run-clang-tidy -p $BUILD_DIR $TIDY_FLAGS \"src/.*|apps/.*\""
    run-clang-tidy -p "$BUILD_DIR" $TIDY_FLAGS "src/.*|apps/.*"
elif command -v run-clang-tidy.py &> /dev/null; then
    echo "Running: run-clang-tidy.py -p $BUILD_DIR $TIDY_FLAGS \"src/.*|apps/.*\""
    run-clang-tidy.py -p "$BUILD_DIR" $TIDY_FLAGS "src/.*|apps/.*"
else
    echo "run-clang-tidy not found. Running clang-tidy manually on files..."
    echo "Using flags: $TIDY_FLAGS"
    find src apps -name "*.cc" -o -name "*.cpp" | xargs clang-tidy -p "$BUILD_DIR" $TIDY_FLAGS
fi
