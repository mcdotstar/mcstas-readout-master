import os

from conan import ConanFile
from conan.tools.build import can_run
from conan.tools.cmake import CMake, cmake_layout


class ReadoutTestPackage(ConanFile):
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain", "VirtualRunEnv"

    def requirements(self):
        self.requires(self.tested_reference_str)

    def layout(self):
        cmake_layout(self)

    def build(self):
        cmake = CMake(self)
        cmake.configure()
        cmake.build()

    def test(self):
        if can_run(self):
            # the C-API consumer links Readout::readout via the packaged ReadoutConfig.cmake
            self.run(os.path.join(self.cpp.build.bindir, "consumer"), env="conanrun")
            # the self-locating tools resolve their packaged resources
            self.run("readout-config --version", env="conanrun")
            self.run("readout-config --show compdir", env="conanrun")
            self.run("readout-replay --help", env="conanrun")
