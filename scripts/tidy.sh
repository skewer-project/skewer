#!/usr/bin/env bash

# Runs clang-tidy on the codebase using compile_commands.json

# Use build/tidy as the default build directory to keep the root clean.
BUILD_DIR="build/tidy"
THREADS=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 1)
FIX_MODE=false

# Parse arguments using a while loop for better reliability
while [[ $# -gt 0 ]]; do
    case $1 in
        --fix)
            FIX_MODE=true
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

# Regex to filter files
FILE_REGEX="(apps|skewer/src|loom/src|libs/exrio/src)/.*"

# Use run-clang-tidy if available, otherwise fallback to finding files
if command -v run-clang-tidy &> /dev/null || command -v run-clang-tidy.py &> /dev/null; then
    TIDY_CMD="run-clang-tidy"
    command -v run-clang-tidy.py &> /dev/null && TIDY_CMD="run-clang-tidy.py"
    
    TIDY_FLAGS="-p $BUILD_DIR -j $THREADS"
    if [ "$FIX_MODE" = true ]; then
        # The wrapper doesn't support -fix-errors, so we disable WarningsAsErrors 
        # for this run to allow -fix to work.
        TIDY_FLAGS="$TIDY_FLAGS -fix -warnings-as-errors=''"
    fi
    
    echo "Running: $TIDY_CMD $TIDY_FLAGS \"$FILE_REGEX\""
    eval "$TIDY_CMD $TIDY_FLAGS \"$FILE_REGEX\""
else
    echo "run-clang-tidy not found. Running clang-tidy manually on files..."
    TIDY_FLAGS="-p $BUILD_DIR"
    if [ "$FIX_MODE" = true ]; then
        TIDY_FLAGS="$TIDY_FLAGS -fix -fix-errors"
    fi
    
    # Find files and run clang-tidy in parallel using xargs
    find apps skewer/src loom/src libs/exrio/src -name "*.cc" -o -name "*.cpp" -o -name "*.h" | xargs -P "$THREADS" -I {} clang-tidy $TIDY_FLAGS {}
fi
