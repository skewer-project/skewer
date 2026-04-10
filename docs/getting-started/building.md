# Building

## Using CMake Presets

The project uses CMake presets for consistent builds:

```bash
# List available presets
cmake --list-presets

# Build with a preset
cmake --preset release
cmake --build --preset release --parallel
```

## Available Presets

| Preset | Description |
|--------|-------------|
| `relwithdebinfo` | Debug build with optimizations enabled |
| `debug` | Debug build with symbols |
| `release` | Optimized release build |
| `ci` | CI build with tests enabled |
| `tidy` | Build with clang-tidy |

## Build Outputs

After building, binaries are located in `build/<preset>/`:

```
build/relwithdebinfo/
├── skewer/           # skewer-render, skewer-worker
├── loom/             # loom, loom-worker
├── libs/exrio/       # exrio library
└── api/              # compiled protobuf files
```

## Running Tests

```bash
# Build and run tests
cmake --preset ci
cmake --build --preset ci --parallel
ctest --preset ci
```

## Building the CLI

The Go CLI is built separately:

```bash
# Build CLI
go build -o skewer-cli ./orchestration/cmd/cli/

# Build Coordinator
go build -o coordinator ./orchestration/cmd/coordinator/
```

## Troubleshooting

### Missing dependencies

If CMake can't find dependencies, you may need to specify paths:

```bash
cmake --preset release -DCMAKE_PREFIX_PATH=/path/to/libs
```

### Protobuf generation

If you modify `.proto` files, regenerate the Go code:

```bash
bash scripts/gen_proto.sh
```
