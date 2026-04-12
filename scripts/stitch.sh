#!/usr/bin/env bash

# Check if at least 4 arguments are provided
if [ "$#" -lt 4 ]; then
    echo "Usage: $0 <fps> <folder_path> <file_prefix> <extension> [start_number] [end_number]"
    echo "Example (PNG):       $0 24 ./frames jump- png"
    echo "Example (EXR):       $0 24 ./frames jump- exr 11"
    echo "Example (EXR 11-50): $0 24 ./frames jump- exr 11 50"
    exit 1
fi

FPS=$1
FOLDER=$2
PREFIX=$3
EXT=$4
START_NUM=${5:-1} # Defaults to 1
END_NUM=$6

# Remove trailing slash from folder path
FOLDER="${FOLDER%/}"

INPUT_PATTERN="$FOLDER/${PREFIX}%04d.${EXT}"
OUTPUT_FILE="${PREFIX}output.mp4"

echo "Stitching .$EXT frames from $FOLDER at $FPS FPS..."
echo "Starting at frame: $START_NUM"

# Build the base ffmpeg command
FFMPEG_CMD=(ffmpeg -framerate "$FPS" -start_number "$START_NUM" -i "$INPUT_PATTERN")

# If an end number is provided, calculate how many frames to grab
if [ -n "$END_NUM" ]; then
    echo "Ending at frame: $END_NUM"
    FRAMES_TO_PROCESS=$(( END_NUM - START_NUM + 1 ))
    
    if [ "$FRAMES_TO_PROCESS" -le 0 ]; then
        echo "Error: End number ($END_NUM) must be greater than or equal to start number ($START_NUM)."
        exit 1
    fi
    
    FFMPEG_CMD+=(-frames:v "$FRAMES_TO_PROCESS")
fi

# Add the final encoding settings. 
# EXRs are 16/32-bit float, so forcing yuv420p is strictly necessary here to make a playable MP4.
FFMPEG_CMD+=(-c:v libx264 -pix_fmt yuv420p "$OUTPUT_FILE")

# Run the constructed command
"${FFMPEG_CMD[@]}"

# Check if successful
if [ $? -eq 0 ]; then
    echo "Success! Video saved as $OUTPUT_FILE in the current directory."
else
    echo "Error: ffmpeg failed. Double-check your starting frame exists and the extension is correct!"
fi