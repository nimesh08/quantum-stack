# photon/bindings/CMakeLists.txt
#
# Two layers:
#   1. photon_engine_cpp   - pure C++ wrapper (always builds when the
#                            engine libs build).
#   2. photon._engine      - nanobind Python extension. Built only when
#                            nanobind is found via find_package.

# (1) Always-on C++ wrapper.
add_library(photon_engine_cpp STATIC
  lib/Engine.cpp
)

set_target_properties(photon_engine_cpp PROPERTIES POSITION_INDEPENDENT_CODE ON)

target_include_directories(photon_engine_cpp PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/include
)

target_link_libraries(photon_engine_cpp PUBLIC
  phonon_dialect
  phonon::dialect
  spinor::dialect
)

# Pull in the actual phonon parser/types/lower/optimizer libraries.
# These are the compiled engine.
foreach(lib IN ITEMS phonon_parser phonon_types phonon_lower phonon_optimizer)
  if(TARGET ${lib})
    target_link_libraries(photon_engine_cpp PUBLIC ${lib})
  endif()
endforeach()

# (2) nanobind Python extension.
# nanobind requires Python found first (specifically the Python::Module
# target, which find_package(Python ...) provides).
find_package(Python COMPONENTS Interpreter Development.Module QUIET)

find_package(nanobind ${QSTACK_NANOBIND_VERSION} CONFIG QUIET)
if(NOT nanobind_FOUND AND Python_Interpreter_FOUND)
  # Try the pip-installed nanobind cmake dir.
  execute_process(
    COMMAND ${Python_EXECUTABLE} -c "import nanobind, sys; sys.stdout.write(nanobind.cmake_dir())"
    OUTPUT_VARIABLE _NB_DIR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE _NB_RC)
  if(_NB_RC EQUAL 0 AND _NB_DIR)
    list(PREPEND CMAKE_PREFIX_PATH ${_NB_DIR})
    find_package(nanobind CONFIG QUIET)
  endif()
endif()

if(nanobind_FOUND)
  message(STATUS "photon: nanobind found (${nanobind_VERSION}); building photon._engine")
  nanobind_add_module(photon_engine_pymod
    NB_STATIC
    python/Module.cpp
  )
  set_target_properties(photon_engine_pymod PROPERTIES
    OUTPUT_NAME "_engine"
    # A generator expression prevents Visual Studio and other multi-config
    # generators from appending Release/Debug after the staged package path.
    LIBRARY_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/photon/bindings/python/photon>"
    RUNTIME_OUTPUT_DIRECTORY "$<1:${CMAKE_BINARY_DIR}/photon/bindings/python/photon>"
  )
  target_link_libraries(photon_engine_pymod PRIVATE photon_engine_cpp)
  set(QSTACK_HAVE_NANOBIND ON CACHE INTERNAL "nanobind extension built" FORCE)
else()
  set(QSTACK_HAVE_NANOBIND OFF CACHE INTERNAL "nanobind extension built" FORCE)
  message(STATUS "photon: nanobind not found; skipping photon._engine extension. "
                 "Pass -Dnanobind_DIR=$(python3 -c 'import nanobind; print(nanobind.cmake_dir())') to enable.")
endif()

# Stage the Python facade package layout under build/. The package
# sources live at photon/frontends/python/photon/; the nanobind
# extension's .so is dropped into the same directory so the package
# imports as a single unit. We stage at build time (not configure time)
# so edits to .py files don't require a re-run of cmake.
file(GLOB_RECURSE _PHOTON_PY_SRCS
     LIST_DIRECTORIES false
     CONFIGURE_DEPENDS
     ${CMAKE_SOURCE_DIR}/photon/frontends/python/photon/*.py)

set(_PHOTON_PY_DEST ${CMAKE_BINARY_DIR}/photon/bindings/python/photon)
file(MAKE_DIRECTORY ${_PHOTON_PY_DEST})

set(_PHOTON_PY_STAGED)
foreach(_src IN LISTS _PHOTON_PY_SRCS)
  get_filename_component(_name ${_src} NAME)
  set(_dst ${_PHOTON_PY_DEST}/${_name})
  add_custom_command(
    OUTPUT ${_dst}
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${_src} ${_dst}
    DEPENDS ${_src}
    VERBATIM)
  list(APPEND _PHOTON_PY_STAGED ${_dst})
endforeach()
add_custom_target(photon_python_stage ALL DEPENDS ${_PHOTON_PY_STAGED})

if(TARGET photon_engine_pymod)
  add_dependencies(photon_engine_pymod photon_python_stage)
endif()


# A wheel is populated by CMake install rules, not by copying files into a
# build directory. Defer until all three frontend targets have been created.
if(QSTACK_BUILD_PYTHON_WHEEL)
  function(qstack_install_photon_wheel)
    if(NOT TARGET photon_engine_pymod)
      message(FATAL_ERROR "A Photon wheel requires the nanobind extension")
    endif()
    install(TARGETS photon_engine_pymod
      LIBRARY DESTINATION photon
      RUNTIME DESTINATION photon)
    install(DIRECTORY "${CMAKE_SOURCE_DIR}/photon/frontends/python/photon/"
      DESTINATION photon FILES_MATCHING PATTERN "*.py")
    install(FILES "${CMAKE_SOURCE_DIR}/LICENSE" DESTINATION photon)
    foreach(directory IN ITEMS chips topologies)
      install(DIRECTORY "${CMAKE_SOURCE_DIR}/spinor/registry/${directory}"
        DESTINATION photon/registry FILES_MATCHING PATTERN "*.yaml")
    endforeach()
    foreach(compiler IN ITEMS spinorc phononc photonc photonc-cxx)
      if(NOT TARGET ${compiler})
        message(FATAL_ERROR "A Photon wheel requires ${compiler}")
      endif()
      install(TARGETS ${compiler} RUNTIME DESTINATION photon/bin)
    endforeach()
  endfunction()
  cmake_language(DEFER DIRECTORY "${CMAKE_SOURCE_DIR}" CALL qstack_install_photon_wheel)
endif()
