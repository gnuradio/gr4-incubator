# gr4-incubator

The gr4-incubator repo is designed to be a staging area for GR4 block development prior to 
upstreaming blocks to the main gnuradio4 repo.  It is intended to collect and triage the "kitchen sink"
of useful blocks, schedulers, utilities, but have a lower bar for accepting contributions
than the core.

## Getting Started

Pre-requisites for compiling this codebase are outlined in `docker/Dockerfile`.  For convenience,
example `devcontainer.json` configurations are given in the `.devcontainer` directory for VS Code integration

## Organization

The tree is organized around creating multiple modules under the `blocks` directory that live 

```
├── blocks
│   ├── analog
│   │   ├── apps
│   │   ├── benchmarks
│   │   ├── docs
│   │   ├── examples
│   │   ├── include
│   │   ├── plugin
│   │   ├── src
│   │   └── test
│   ├── audio
│   ├── basic
│   ├── pfb
│   ├── soapysdr
│   └── zeromq
├── cmake
│   └── Modules
├── docker
├── examples
│   └── gr3_flowgraphs
```



## Examples

Examples that exercise multiple modules

## Build instructions (CMake)

The CMake path is the active build path for this branch.

### Build policy

- System dependencies only (`find_package` / `pkg-config`); no vendored dependency subprojects or `FetchContent`.
- Plugins are hard-disabled in CMake (`ENABLE_PLUGINS=ON` is an error).
- GUI examples are optional and `OFF` by default.

### Configure and build

```bash
cmake -S . -B build -G Ninja \
  -DENABLE_EXAMPLES=ON \
  -DENABLE_TESTING=ON \
  -DENABLE_GUI_EXAMPLES=OFF

cmake --build build -j
```

### Run tests

```bash
ctest --test-dir build --output-on-failure
```

### Enable GUI examples

`ENABLE_GUI_EXAMPLES=ON` requires `imgui`, `implot`, `glfw3`, and `OpenGL`.

If `implot` is not discoverable through package config or `pkg-config`, pass explicit paths:

```bash
cmake -S . -B build -G Ninja \
  -DENABLE_EXAMPLES=ON \
  -DENABLE_TESTING=ON \
  -DENABLE_GUI_EXAMPLES=ON \
  -DIMPLOT_INCLUDE_DIR=/usr/local/include \
  -DIMPLOT_LIBRARY=/usr/local/lib/libimplot.so
```

You can also use:

```bash
-DIMPLOT_SOURCE_DIR=/path/to/implot
```

### Common Ubuntu devcontainer packages

- `librtaudio-dev`
- `libcli11-dev`
- `libimgui-dev`
- `libglfw3-dev`
- `libopengl-dev`

`implot` is typically not packaged; in this repo's devcontainer it is installed in `/usr/local`.

## TODOs and upstream gating factors

The current CMake setup is intentionally pragmatic. Full canonical cleanup is gated by upstream work in GNU Radio 4 and related packaging.

### Local TODOs in gr4-incubator

- Add install/export package config for this repo (`install(EXPORT ...)`, `gr4-incubatorConfig.cmake`).
- Reduce custom fallback logic for GUI dependency discovery once upstream/system packages are stable.

### Upstream gating factors (gnuradio4 and ecosystem)

- Stable exported CMake package targets from `gnuradio4` for all required link interfaces.
- Canonical target/package exposure for blocklib components currently consumed as raw libraries.
- Consistent distro packaging (or official CMake package configs) for optional GUI dependencies, especially `implot`.
- Clear upstream contract for optional/plugin mechanisms in CMake.
