set(CURRENT_LIST_DIR ${CMAKE_CURRENT_LIST_DIR})

find_package(Python3 QUIET COMPONENTS Interpreter REQUIRED)

if (NOT DEFINED gen_dir)
  set(gen_dir ${CMAKE_BINARY_DIR}/gen)
endif()
if (NOT DEFINED src_dir)
  # issuing, e.g., `cmake -S {source} -B {build} && cmake --build {directory}` gives
  # CMAKE_SOURCE_DIR={source} for the first call and CMAKE_SOURCE_DIR={directory} for the second
  set(src_dir ${CMAKE_SOURCE_DIR})
endif()

set(post_configure_file ${gen_dir}/version.hpp)

function(checkVersion version)
  set(version_arg)
  if (DEFINED project_version AND NOT "${project_version}" STREQUAL "")
    set(version_arg --version=${project_version})
  endif()
  execute_process(
          COMMAND ${Python3_EXECUTABLE} ${CURRENT_LIST_DIR}/version.py ${src_dir} ${gen_dir} ${version_arg}
          OUTPUT_VARIABLE returned_version
          OUTPUT_STRIP_TRAILING_WHITESPACE
          ECHO_ERROR_VARIABLE
          RESULT_VARIABLE result
  )
  set(${version} ${returned_version} PARENT_SCOPE)
endfunction()

function(checkSetup name)
  if (NOT DEFINED project_version)
    # scikit-build-core already resolved the version via setuptools_scm;
    # reuse it instead of re-deriving from git (absent in an sdist build)
    if (DEFINED SKBUILD_PROJECT_VERSION_FULL)
      set(project_version ${SKBUILD_PROJECT_VERSION_FULL})
    elseif (DEFINED SKBUILD_PROJECT_VERSION)
      set(project_version ${SKBUILD_PROJECT_VERSION})
    endif()
  endif()
  add_custom_target(AlwaysCheck COMMAND ${CMAKE_COMMAND}
    -DRUN_CHECK=1
    -Dgen_dir=${gen_dir}
    -Dsrc_dir=${src_dir}
    "-Dproject_version=${project_version}"
    -P ${CURRENT_LIST_DIR}/check.cmake
    BYPRODUCTS ${post_configure_file}
  )
  checkVersion(CHECK_VERSION)
  if (NOT DEFINED CHECK_VERSION)
    set(CHECK_VERSION "0.0.0")
  endif()
  set("${name}_VERSION" ${CHECK_VERSION} PARENT_SCOPE)
endfunction()

function(addCheckDependency target)
  target_include_directories(${target} PUBLIC
          $<BUILD_INTERFACE:${gen_dir}>
          $<INSTALL_INTERFACE:>  # <prefix>
  )
  add_dependencies(${target} AlwaysCheck)
endfunction()

if (RUN_CHECK)
  checkVersion(CHECK_VERSION)
  if (NOT DEFINED CHECK_VERSION)
    set(SAFE_VERSION "0.0.0")
  endif()
endif()
