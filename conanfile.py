from conan import ConanFile
from conan.tools.cmake import cmake_layout


class ReadoutRecipe(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeToolchain", "CMakeDeps"
    default_options = {
        "hdf5/*:shared": False,
        "hdf5/*:hl": False,
        "hdf5/*:with_zlib": False,
        "highfive/*:with_boost": False,
        "highfive/*:with_eigen": False,
        "highfive/*:with_xtensor": False,
        "highfive/*:with_opencv": False,
    }

    def requirements(self):
        self.requires("catch2/3.6.0")
        self.requires("highfive/2.10.0")
        self.requires("nlohmann_json/3.11.3")

    def layout(self):
        cmake_layout(self)
