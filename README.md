# Skewer
A physically-based ray tracer capable of deep rendering and compositing

## Building

### Dependencies
Ensure you have the following installed before building:
- **C++ Compiler** (C++17 support required)
    * *Linux:* GCC or Clang
    * *macOS:* Xcode Command Line Tools (`xcode-select --install`)
    * *Windows:* MSVC (Visual Studio) or MinGW
- **CMake** (Version 3.14 or higher)
- **libopenexr** and **libimath**
  - MacOS hosts can install both with `brew install openexr`
  - Linux hosts require both `libopenexr-dev` and `libimath-dev` packages

The following libraries are included in the source or fetched automatically by CMake. **You do not need to install these manually.**
* [nlohmann/json](https://github.com/nlohmann/json) (via CMake FetchContent)
* [stb_image](https://github.com/nothings/stb) (Vendored)
* [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) (Vendored)

### Compiling

This project uses CMake. To build the executable:

```bash
# 1. Create a build directory
mkdir build

# 2. Configure the project
cmake -B ./build/ -S ./

# 3. Compile
cmake --build ./build/ --parallel
```

## Usage

```bash
# Linux/macOS
./build/skewer-render <scene json file>

# Windows (Command Prompt)
./build/skewer-render.exe <scene json file>
```

### Scenes
The renderer accepts a scene config defined a json file.
Examples of these files can be found in the following [folder on Google Drive](https://drive.google.com/drive/folders/1qi5UjEE2lD4gkOMuiLxX3ULs95huyOQD?usp=drive_link).

Copy both the `objects` and `scenes` folder into the project root and invoke `skewer-renderer` with a scene json file in the scenes folder.
For example:
```bash
./build/skewer-render scenes/cornell_box.json  
```

### Help
`./build/skewer-render --help`

The renderer outputs both a `.ppm` and `.exr` file. Open either to verify the image rendered correctly.

## Authors
- [AkshatAdsule](https://github.com/AkshatAdsule)
- [yooian](https://github.com/yooian)
- [shavolkov](https://github.com/shavolkov)
- [C3viche](https://github.com/C3viche)
