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
if(NOT nanobind_FOUND)
  # Try the pip-installed nanobind cmake dir.
  execute_process(
    COMMAND ${CMAKE_COMMAND} -E env python3 -c "import nanobind, sys; sys.stdout.write(nanobind.cmake_dir())"
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
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/photon/bindings/python/photon"
  )
  target_link_libraries(photon_engine_pymod PRIVATE photon_engine_cpp)
  set(QSTACK_HAVE_NANOBIND ON CACHE INTERNAL "nanobind extension built")
else()
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

# ----------------------------------------------------------------------------
# Wheel-only assets: when scikit-build-core drives the build with
# QSTACK_BUILD_PYTHON_WHEEL=ON, we additionally:
#   1. Copy the chip registry into photon/registry/ inside the wheel layout.
#   2. Drop the `spinorc` and `photonc` CLI binaries into photon/bin/ so
#      `pip install heisenberg-photon` puts them on $PATH automatically.
# ----------------------------------------------------------------------------
if(QSTACK_BUILD_PYTHON_WHEEL)
  message(STATUS "photon: building Python wheel assets (registry + spinorc + photonc)")

  set(_PHOTON_REGISTRY_SRC ${CMAKE_SOURCE_DIR}/spinor/registry)
  set(_PHOTON_REGISTRY_DST ${_PHOTON_PY_DEST}/registry)
  add_custom_command(
    OUTPUT ${_PHOTON_REGISTRY_DST}/.staged
    COMMAND ${CMAKE_COMMAND} -E rm -rf ${_PHOTON_REGISTRY_DST}
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${_PHOTON_REGISTRY_SRC} ${_PHOTON_REGISTRY_DST}
    COMMAND ${CMAKE_COMMAND} -E touch ${_PHOTON_REGISTRY_DST}/.staged
    DEPENDS ${_PHOTON_REGISTRY_SRC}
    COMMENT "Stage chip registry into the photon wheel layout"
    VERBATIM)
  add_custom_target(photon_wheel_registry ALL
    DEPENDS ${_PHOTON_REGISTRY_DST}/.staged)
  add_dependencies(photon_python_stage photon_wheel_registry)

  set(_PHOTON_BIN_DST ${_PHOTON_PY_DEST}/bin)
  file(MAKE_DIRECTORY ${_PHOTON_BIN_DST})

  # Copy spinorc + photonc once they are built. We use POST_BUILD on the
  # CLI targets so the staging follows the link step automatically.
  if(TARGET spinorc)
    add_custom_command(TARGET spinorc POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:spinorc> ${_PHOTON_BIN_DST}/spinorc
      COMMENT "Stage spinorc binary into the photon wheel layout")
  endif()
  if(TARGET photonc)
    add_custom_command(TARGET photonc POST_BUILD
      COMMAND ${CMAKE_COMMAND} -E copy_if_different
              $<TARGET_FILE:photonc> ${_PHOTON_BIN_DST}/photonc
      COMMENT "Stage photonc binary into the photon wheel layout")
  endif()
endif()
