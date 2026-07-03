# Overarching Goal
Restructure this library to be installed via Conda/Mamba (via Conda-Forge), Conan, or Pip (via PyPI).

We can and should learn from the `mcpl` and `ncrystal` projects which have successfully implemented C++ libraries with Python bindings and Conda packaging.
Both of these projects' repositories are inside the `references/` folder within this directory.
The `mcpl` project is slightly simpler and may be more appropriate as a reference for our implementation.

The current structure of readout is based on an earlier form of mcpl, which is another reason that following its lead is probably worthwhile.

# The mcpl pattern (what we are copying)

Reading references/mcpl (v2.2.8), the load-bearing ideas are:

1. **A monorepo of separately installable packages**: `mcpl_core` (the native
   library + CLI tools, a standalone CMake project that is *also* pip-installable
   via scikit-build-core), `mcpl_python` (pure-Python API, setuptools),
   `mcpl_extra` (optional apps), `mcpl_metapkg` (`pip install mcpl` = the bundle).
   The repo-root CMakeLists.txt exists only to build everything and run ctest in
   development/CI; it is not what packages build.
2. **Self-locating config tool**: `mcpl-config` is a compiled binary that
   resolves every resource path *relative to its own location*
   (`mcplcfg_resolverelpath(bindir, relpath)` in mcpl_core/app_config/main.c),
   with the relative paths baked in at configure time per install layout. One
   mechanism covers the wheel layout, the standard CMake install, and the build
   tree — this is the generalization of our READOUT_DEVELOPMENT_MODE hack, and
   the fix for the PATH-inheritance problem DEVELOPMENT.md describes.
3. **The wheel is just a container**: no Python C-API anywhere. scikit-build-core
   builds the ordinary CMake project into the wheel; a tiny CMake-*generated*
   shim package (`skbld_autogen/_mcpl_core`, from cmake/template_pymod.py.in)
   provides console entry points that exec the bundled binaries, plus a
   `cmakedir()` helper so downstream CMake can `find_package` a wheel-installed
   library. Wheels are `py3`/ABI-agnostic.
4. **One VERSION file** at the repo root, injected into CMake and every
   pyproject.
5. **conda-forge is downstream**: feedstocks build the same CMake project from
   release tarballs with dependencies from conda-forge; nothing conda-specific
   lives in the repo beyond a sane install layout.

# What is different for readout (the real work)

- **We have external dependencies; mcpl has none.** HDF5 (linked), HighFive +
  nlohmann_json (header-only, via conan today), cluon/ctream/args.hxx (already
  vendored). Conda and Conan get HDF5 from their ecosystems — easy. **PyPI wheels
  must bundle a static libhdf5** (the h5py approach): build static HDF5 inside
  cibuildwheel (or FetchContent it when building wheels) and let auditwheel
  verify nothing leaks. This is the highest-risk item and must be de-risked
  first (phase L2 starts with a spike).
- **McStas components are runtime data**: share/Readout/*.comp + lib-readout.{h,c}
  are located by mccode-antlr through `readout-config --show compdir` and the
  components' `CMD(readout-config ...)` DEPENDENCY lines. The self-locating
  config pattern serves this directly, and it is the user-facing win:
  `pip install <core>` puts readout-config on PATH and instruments just work.
- **C++ consumers exist**: mccode-plumber will link libreadout and call
  `replay()` with its EPICS ParameterPublisher, so the installed package must
  export usable headers (replay.h, reader.h, CollectorClass.h, SenderConfigs.h,
  the C API headers) and a working `find_package(Readout)` — the current
  install(EXPORT) is a starting point but the public/private header split must
  be made deliberate.
- **No Python API exists yet** — so no `readout_python` and no metapackage in
  the first pass. The wheel ships binaries + components only, exactly like
  mcpl-core's "empty" Python surface.

# Decisions (settled 2026-07-02)

- **Package naming**: `mcstas-readout-master` — this matches the GitHub
  upstream (g5t/mcstas-readout-master) and the conda-forge feedstock that
  ALREADY EXISTS (conda-forge/mcstas-readout-master-feedstock, currently
  building v0.3.3 from the release tag with conda-forge hdf5/highfive/catch2).
  The PyPI distribution takes the same name. Repo directory `readout_core/`,
  CMake package and binary names unchanged.
- **Feedstock compatibility constraints on the restructure**: the existing
  feedstock tests check `readout-config --help/--version`, `readout-replay
  --help`, the presence of the installed `Readout.h`, and
  `cmake-package-check Readout --targets Readout`. The L1 install layout must
  keep the `Readout` CMake package name and exported target working; feedstock
  updates (new version, readout-combine test, any path adjustments) land
  together with the first post-restructure release.
- **Windows**: keep the existing WIN32 shims compiling but target Linux + macOS
  wheels first; Windows wheels only if someone asks.
- **Legacy sources** (readout_orig, Readout_merge, discrete path, Array, tester
  helpers) move into readout_core/src unchanged — restructuring is not the time
  to prune features.

# Phases

## L1 — Repo restructure + self-locating readout-config (no packaging yet)

**Status: DONE (2026-07-03).** Notes against the original text: readout-config
was already self-locating (helper.cpp resolves baked relative paths against
the binary via /proc/self/exe, _NSGetExecutablePath, GetModuleFileName), so
the work was unifying the layouts rather than rewriting the tool — the build
tree now mirrors the install prefix (bin/, lib{,64}/, include/, share/Readout)
and one set of baked relative paths serves both; READOUT_DEVELOPMENT_MODE and
run_tests.sh are deleted. Public headers install flat (include/Readout.h stays
where the feedstock checks it) rather than under include/readout/. The full
C++ consumer surface plus generated version.hpp now installs; nlohmann_json
became a PRIVATE link so find_package(Readout) has no third-party
requirements; C++ consumers bring HighFive themselves. devel/check_install.sh
verifies an installed prefix end-to-end (readout-config resolution, a scratch
find_package(Readout) C consumer — which caught missing stdint.h/stddef.h
includes in the C API headers — and the pytest run-tests executed against the
installed prefix via READOUT_BUILD_DIR).

1. Move to the mcpl shape:
   - `readout_core/src/` (everything in lib/ except public headers),
     `readout_core/include/readout/` (deliberate public set: the C API headers,
     Readout.h umbrella, and the C++ consumer headers reader.h/replay.h/
     CollectorClass.h/Sender*.h/enums.h/Structs.h/hdf_interface.h + generated
     version.hpp), `readout_core/app_config/`, `readout_core/app_replay/`,
     `readout_core/app_combine/`, `readout_core/components/` (share/Readout),
     `readout_core/cmake/`, root `tests/` + `test/` stay as the dev/CI suite
     driven by the root CMakeLists (mcpl style).
   - `VERSION` file at root as the single version source (feeds CMake and later
     the pyprojects).
2. Rewrite readout-config on the mcpl model: paths resolved relative to the
   binary, per-layout relative paths baked at configure time (build-tree,
   standard install, wheel). Delete READOUT_DEVELOPMENT_MODE and the
   header-copy/symlink machinery — the build-tree layout replaces it, and
   run_tests.sh/DEVELOPMENT.md shrink accordingly.
3. Modernize install: namespaced `install(TARGETS ... EXPORT)`, public headers
   as a FILE_SET, components + lib-readout.{h,c} to a data dir, ReadoutConfig
   package files. Acceptance: ctest 94/94 and pytest suites green against BOTH
   the build tree and an installed prefix (new CI-able script), and
   `find_package(Readout)` works from a scratch consumer project.

## L2 — PyPI wheel (scikit-build-core), HDF5 spike first

**Status: DONE locally (2026-07-03); CI wheels await a push/release.** Notes:
following brille (not mcpl) on two points — the build backend is
scikit-build-core-conan with the existing repo-root conanfile.py supplying
static HDF5, and pyproject.toml lives at the repo root since there is exactly
one installable package. The shim is a static python/_readout_core package
(not CMake-generated like mcpl's): entry points exec the bundled binaries
under _readout_core/data, plus a cmakedir() helper. Wheels are py3-none
(no Python C-API). The symbol-clash spike resolved into an explicit
READOUT_HIDE_BUNDLED_SYMBOLS toggle: ON for wheels (nm shows zero H5 exports,
readout API intact), OFF otherwise because in-process C++ consumers running
header-inline HighFive code must bind the library's HDF5 — hiding it hands
them a second static copy and 24 tests fail (empirically confirmed).
Verified locally: fresh-venv wheel install, all three tools run, no dynamic
HDF5 dependency, and the mccode-antlr pytest suite passes against the wheel
installation (READOUT_BUILD_DIR=<site-packages>/_readout_core/data) with
h5py present. .github/workflows/wheels.yml builds sdist + manylinux/macOS
wheels via cibuildwheel and publishes on GitHub releases (trusted publishing,
'pypi' environment — needs one-time PyPI-side setup).

1. **Spike (de-risking, prior art exists)**: HDF5-bundled wheels have been
   built for this project before and are known to work, but require care to
   avoid symbol clashes — a statically bundled libhdf5 must not export symbols
   that collide with another HDF5 loaded in the same process (h5py being the
   obvious cohabitant). Consult the recent changes in the `brille` project as
   the guideline when this work starts (symbol visibility / exclude-libs
   treatment). Build a manylinux wheel with static HDF5 + HighFive +
   nlohmann_json via FetchContent behind a READOUT_BUNDLED_DEPS flag; auditwheel
   must pass, `nm`/`objdump` must show no exported HDF5 symbols, and a fresh
   venv (with h5py imported alongside) must run readout-config/replay/combine
   and an mccode-antlr compile+run of CollectorCAEN.
2. `readout_core/pyproject.toml` (scikit-build-core), CMake-generated
   `_readout_core` shim package with entry points for the three tools and a
   `cmakedir()` helper; sdist includes src/include/components/cmake.
3. cibuildwheel config (linux x86_64 + macOS arm64/x86_64) in GitHub Actions;
   TestPyPI first.

## L3 — Conan recipe

**Status: DONE (2026-07-03).** conanfile.py is now a full recipe serving three
roles from one file: dev-dependency provider (in-tree conan provider),
wheel-backend input (scikit-build-core-conan — which copies the recipe to a
temp dir, so set_version falls back gracefully when VERSION is absent), and a
publishable package (`conan create .` builds, packages via cmake.install, and
test_package/ verifies a find_package(Readout) C consumer plus the
self-locating tools from the package cache). package_info uses
cmake_find_mode=none + builddirs so consumers get the project's own
relocatable ReadoutConfig.cmake; highfive is a transitive_headers requirement
(C++ header consumers need it), nlohmann_json is visible=False (PRIVATE
link), catch2 is a test_requires. Publishing to a remote (conancenter or a
private remote) is a separate decision — the recipe is ready.

## L4 — conda-forge (feedstock update, not staged-recipes)

The feedstock already exists: conda-forge/mcstas-readout-master-feedstock.
After the first tagged release with the L1 layout: bump version/sha, adjust
for any install-path changes, add nlohmann_json to host deps if it stops being
vendored, and extend the tests (readout-combine --help, the component data
dir via readout-config --show compdir).

## L5 — deferred until wanted

`readout_python` (h5py-based Reader conveniences, replay orchestration) and a
`mccode-readout` metapackage, mcpl_python/mcpl_metapkg style.

# Risks

- **HDF5-in-wheel** is the make-or-break item; hence the L2 spike before any
  other L2 work.
- The restructure moves every file; do it as pure `git mv` + path fixes in one
  commit series with the full test matrix green at each step, no behavior
  changes mixed in.
- mccode-antlr caches component paths (~/.cache/mccodeantlr and .comp.json
  files); layout changes must be verified with cleared caches.

