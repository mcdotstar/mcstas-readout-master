# Installation

## Build and install from source

```bash
git clone https://github.com/g5t/mcstas-readout-master.git
cmake -S mcstas-readout-master -B mcstas-readout-master-build -DCMAKE_INSTALL_PREFIX="$HOME/.local"
cmake --build mcstas-readout-master-build --target install -j
```

This installs:

- `readout-config`
- `readout-replay`
- `readout-combine`
- `libreadout` and public headers
- McStas component files in `share/Readout`

## Verify the installation

```bash
readout-config --show compdir
readout-config --show libdir
readout-combine --help
readout-replay --help
```

## Local developer build (without install)

The build tree mirrors the install layout. A direct build is enough:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Then expose `build/bin` on `PATH` so McStas tools resolve `readout-config`.

## McStas version note

`SEARCH SHELL "readout-config --show compdir"` requires McStas 3.3+.
For McStas 3.2, copy the `.comp` files to a known McStas component search
directory and omit `SEARCH SHELL`.
