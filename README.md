# Skewer
A physically-based ray tracer capable of deep rendering and compositing

## Build Instructions
This project uses CMake. To build the executable:

```bash
# 1. Create a build directory
mkdir build
cd build

# 2. Configure the project
cmake ..

# 3. Compile
cmake --build .
```

## Usage
### Run the Demo Scene
Renders the built-in demo scene (spheres and triangles).

```bash
# Linux/macOS
`./skewer-render --name output.ppm`

# Windows (Command Prompt)
`skewer-render.exe --name output.ppm`
```

### Help
`./skewer-render --help`

Now open the `.ppm` to see if image renders correctly

## Dependencies

### Prerequisites
Ensure you have the following installed before building:
* **C++ Compiler** (C++17 support required)
    * *Linux:* GCC or Clang
    * *macOS:* Xcode Command Line Tools (`xcode-select --install`)
    * *Windows:* MSVC (Visual Studio) or MinGW
* **CMake** (Version 3.14 or higher)
* **OpenEXR** & **Imath** (Required for high-dynamic-range output)

**macOS (Homebrew)**
```bash
# Installs CMake and OpenEXR (which includes Imath)
brew install cmake openexr
```

**Linux (Ubuntu/Debian)**
```bash
sudo apt-get update
sudo apt-get install build-essential cmake libopenexr-dev libimath-dev
```

### Libraries (Managed Automatically)
The following libraries are included in the source or fetched automatically by CMake. **You do not need to install these manually.**
* [nlohmann/json](https://github.com/nlohmann/json) (via CMake FetchContent)
* [stb_image](https://github.com/nothings/stb) (Vendored)
* [tinyobjloader](https://github.com/tinyobjloader/tinyobjloader) (Vendored)

## Authors
- [AkshatAdsule](https://github.com/AkshatAdsule)
- [yooian](https://github.com/yooian)
- [shavolkov](https://github.com/shavolkov)
- [C3viche](https://github.com/C3viche)
