# spinor/passes/CMakeLists.txt

add_library(spinor_passes STATIC
  lib/CouplingGraph.cpp
  lib/Placement.cpp
  lib/Routing.cpp
  lib/Decomposition.cpp
  lib/Cleanup.cpp
  lib/OptimizationLevel.cpp
  lib/PassManager.cpp
  lib/SynthesisTraits.cpp
  lib/Collect2qBlocks.cpp
  lib/ConsolidateBlocks.cpp
  lib/TwoQubitDecomposer.cpp
  lib/KakResynthesis.cpp
  lib/OneQubitEulerResynthesis.cpp
  lib/CommutativeCancellation.cpp
  lib/OptimizationLoop.cpp
  lib/VF2PostLayout.cpp
)
target_include_directories(spinor_passes
  PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(spinor_passes
  PUBLIC
    spinor::dialect
    spinor::registry
)
target_compile_features(spinor_passes PUBLIC cxx_std_20)

add_library(spinor::passes ALIAS spinor_passes)
