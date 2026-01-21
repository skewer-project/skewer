# Deep Compositing Engine

A C++ implementation of the "Deep Compositing" algorithm, capable of processing and merging samples stored in OpenEXR Deep Scanline format.

## Project Goal
To implement the linear-piecewise opacity merging algorithm used in VFX production (e.g., *Kung Fu Panda*) to combine transparent renders correctly.

## Tech Stack
* **Language:** C++17
* **Build System:** CMake
* **Libraries:** OpenEXR, Imath

## Getting Started

### Prerequisites
* CMake 3.15+
* OpenEXR (Installed via brew/apt)

### Build
```bash
mkdir build && cd build
cmake ..
make
