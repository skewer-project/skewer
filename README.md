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
- CMake (3.14+)
- nlohmann/json (Fetched automatically via CMake)
- stb_image (included in `src`)
- tiny_obj_loader (included in `src`)

## Authors
- [AkshatAdsule](https://github.com/AkshatAdsule)
- [yooian](https://github.com/yooian)
- [shavolkov](https://github.com/shavolkov)
- [C3viche](https://github.com/C3viche)
