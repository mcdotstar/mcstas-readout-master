import os

from conan import ConanFile
from conan.tools.cmake import CMake, cmake_layout
from conan.tools.files import load


class ReadoutRecipe(ConanFile):
    """One recipe, three roles:

    1. dependency provider for development builds (`conan install`, run by
       cmake/conan_provider.cmake during ordinary CMake configuration),
    2. dependency provider for pip wheel builds (run by the
       scikit-build-core-conan backend, see pyproject.toml),
    3. a publishable package recipe (`conan create .`), verified by
       test_package/.
    """

    name = "mcstas-readout-master"
    license = "BSD-3-Clause"
    url = "https://github.com/g5t/mcstas-readout-master"
    homepage = "https://github.com/g5t/mcstas-readout-master"
    description = (
        "McStas components and tools mimicking ESS Readout Master detector readouts: "
        "collect simulated events to HDF5, combine files, and replay them to Event Formation Units"
    )
    topics = ("mcstas", "neutron", "simulation", "ess")

    settings = "os", "compiler", "build_type", "arch"
    package_type = "shared-library"
    default_options = {
        "hdf5/*:shared": False,
        "hdf5/*:hl": False,
        "hdf5/*:with_zlib": False,
        "highfive/*:with_boost": False,
        "highfive/*:with_eigen": False,
        "highfive/*:with_xtensor": False,
        "highfive/*:with_opencv": False,
    }

    generators = "CMakeToolchain", "CMakeDeps"

    exports = "VERSION"
    exports_sources = (
        "VERSION",
        "CMakeLists.txt",
        "cmake/*",
        "readout_core/*",
        "README.md",
        "LICENSE",
    )

    def configure(self):
        if self.settings.os == "Windows":
            # Windows has no dynamic-symbol interposition: with a static HDF5
            # inside readout.dll, any C++ consumer (our tester, mccode-plumber)
            # that compiles the header-inline HighFive API gets a SECOND live
            # HDF5 instance, and hid_t handles cannot cross the DLL boundary
            # ("H5Aexists: invalid identifier"). One shared hdf5.dll gives every
            # module the same instance — which is also what conda-forge ships.
            self.options["hdf5/*"].shared = True

    def set_version(self):
        if self.version:
            return
        # exports = "VERSION" places the file next to the recipe for conan create;
        # the scikit-build-core-conan backend instead copies the recipe to a temp
        # dir, where no VERSION exists — and the dependency-provider role does not
        # need one, so fall back rather than fail.
        for root in (self.recipe_folder, os.getcwd()):
            candidate = os.path.join(root, "VERSION")
            if os.path.isfile(candidate):
                self.version = load(self, candidate).strip()
                return
        self.version = "0.0.0"

    def requirements(self):
        # HighFive (and transitively static HDF5) is linked into libreadout;
        # consumers of the installed C++ headers also compile against the
        # HighFive/HDF5 headers, so the requirement stays visible downstream.
        self.requires("highfive/2.10.0", transitive_headers=True)
        # nlohmann_json is used only inside SenderConfigs.cpp (PRIVATE link):
        self.requires("nlohmann_json/3.11.3", visible=False)

    def build_requirements(self):
        # only the repo-level test suite uses Catch2; it is not built by the recipe
        self.test_requires("catch2/3.6.0")

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure(variables={
            # dependencies come from the toolchain this recipe generated;
            # the in-tree provider must not run a second conan:
            "READOUT_USE_CONAN": "OFF",
            "READOUT_BUILD_TESTS": "OFF",
        })
        cmake.build()

    def package(self):
        cmake = CMake(self)
        cmake.install()

    def package_info(self):
        self.cpp_info.libs = ["readout"]
        # Consumers use the ReadoutConfig.cmake installed by the project itself
        # (relocatable, exports Readout::readout) rather than a CMakeDeps stand-in:
        self.cpp_info.set_property("cmake_find_mode", "none")
        self.cpp_info.builddirs = [os.path.join("lib", "cmake", "Readout")]
        # readout-config (and the other tools) on PATH for consumers' build/run envs:
        bindir = os.path.join(self.package_folder, "bin")
        self.buildenv_info.append_path("PATH", bindir)
        self.runenv_info.append_path("PATH", bindir)
