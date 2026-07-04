# Development guide

## Layout

The repository follows the mcpl pattern (see layout_plan.md): everything that
installs lives in `readout_core/` — library sources in `src/`, public headers
in `include/`, the CLI tools in `app_config`/`app_replay`/`app_combine`, the
McStas components in `components/` — while the root CMakeLists.txt only wires
up toolchain options and the repo-level test suite in `test/` and `tests/`.

## Building

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

Dependencies (HighFive, HDF5, nlohmann_json, Catch2) come through Conan by
default (`READOUT_USE_CONAN=ON`); pass `-DREADOUT_USE_CONAN=OFF` to use system
packages instead.

There are no build modes: **the build tree mirrors the install layout**
(`bin/`, `lib/`, `include/`, `share/Readout/`), and `readout-config` resolves
every resource path relative to its own binary through one set of baked
relative paths. The same binary therefore works unchanged from the build tree,
an installed prefix, or (later) a wheel — and the build tree is directly
usable by mccode-antlr:

```bash
$ build/bin/readout-config --show compdir
/home/user/project/build/share/Readout
```

This replaces the former `READOUT_DEVELOPMENT_MODE` option and the
`run_tests.sh` wrapper, which existed to work around PATH-inheritance problems
when McStas tools spawned `readout-config`; putting `build/bin` on PATH is all
that is needed now.

## Testing

```bash
cd build && ctest                     # C++ unit tests + CLI smoke + integration
python3 -m pytest tests/             # mccode-antlr compile/run tests
```

## Documentation

Install Doxygen, then generate the static HTML site locally. Use a dedicated
build directory with `READOUT_DOCS_ONLY=ON` — it skips conan and every
compiled target, so it configures anywhere without touching your development
build directories:

```bash
cmake -S . -B build-docs -DREADOUT_DOCS_ONLY=ON
cmake --build build-docs --target docs
```

The generated site is written to `build-docs/docs/html/`. The `docs` target
first runs `docs/extract_comp_docs.py`, which turns the McDoc headers
(`%I`/`%D`/`%P` sections) of `readout_core/components/*.comp` into markdown
pages — keep those headers up to date when changing component parameters.
The same headers make the components readable with McStas's `mcdoc`. The site
is published by `.github/workflows/docs.yml` on pushes to `main` (GitHub Pages
must be set to deploy from GitHub Actions in the repository settings).

The pytest suite discovers a build directory by looking for
`<dir>/bin/readout-config` in `build-dev`, `build`, `cmake-build-debug`,
`cmake-build-release` (in that order — rebuild every directory you keep, or
remove the stale ones, before trusting pytest results). The ctest
`integration` test skips (exit 100) when `mcstas-antlr` is not installed.

After editing a `.comp` file, delete any stale `*.comp.json` caches next to it
and the `~/.cache/mccodeantlr` directory so mccode-antlr re-parses.
