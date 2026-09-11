
cmake_minimum_required(VERSION 3.20)

project(scitec_opir_detection
        VERSION 1.0.0
        DESCRIPTION "Scitec OPIR detection practice project"
        LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 15)
    message(FATAL_ERROR
            "GCC 15 or newer is required for the C++23 library features used here "
            "(found ${CMAKE_CXX_COMPILER_VERSION}). See README.")
elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang" AND CMAKE_CXX_COMPILER_VERSION VERSION_LESS 20)
    message(FATAL_ERROR
            "Clang 20 or newer is required for the C++23 library features used here "
            "(found ${CMAKE_CXX_COMPILER_VERSION}). See README.")
endif()

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Emit compile_commands.json so an editor's language server sees the same flags
# and include paths the build uses.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

function(scitect_set_warnings target)
    target_compile_options(${target} PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall;-Wextra;-Wpedantic;-Wconversion;-Wshadow>
    )
endfunction()

# ---------------------------------------------------------------------------
# Core
# ---------------------------------------------------------------------------
add_library(scitec_core INTERFACE)
target_include_directories(scitec_core INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

# ---------------------------------------------------------------------------
# Filter
# ---------------------------------------------------------------------------
add_library(scitec_filter
src/filter/AlphaBeta.cpp
src/filter/Tracker.cpp
)

target_link_libraries(scitec_filter PUBLIC scitec_core)
scitect_set_warnings(scitec_filter)

# ---------------------------------------------------------------------------
# Detect
# ---------------------------------------------------------------------------
add_library(scitec_detect
src/detect/Cfar.cpp
src/detect/Cluster.cpp
)

target_link_libraries(scitec_detect PUBLIC scitec_core)
scitect_set_warnings(scitec_detect)

# ---------------------------------------------------------------------------
# Frame
# ---------------------------------------------------------------------------
add_library(scitec_frame
src/frame/FrameReader.cpp
src/frame/FrameWriter.cpp
)

target_link_libraries(scitec_frame PUBLIC scitec_core)
scitect_set_warnings(scitec_frame)

# ---------------------------------------------------------------------------
# Sim
# ---------------------------------------------------------------------------
add_library(scitec_sim
    src/sim/SceneSimulator.cpp
    src/sim/TruthWriter.cpp
    src/sim/TruthTable.cpp
)
target_link_libraries(scitec_sim PUBLIC scitec_core)
scitect_set_warnings(scitec_sim)

# ---------------------------------------------------------------------------
# Eval
# ---------------------------------------------------------------------------
add_library(scitec_eval
    src/eval/Score.cpp
)
target_link_libraries(scitec_eval PUBLIC scitec_sim scitec_filter)
scitect_set_warnings(scitec_eval)

# ---------------------------------------------------------------------------
# Opir executable
# ---------------------------------------------------------------------------
add_executable(scitec_opir_detection src/opir/main.cpp)
target_link_libraries(scitec_opir_detection PRIVATE scitec_eval scitec_sim scitec_frame scitec_filter scitec_detect)
scitect_set_warnings(scitec_opir_detection)

# ---------------------------------------------------------------------------
# Sim Executable
# ---------------------------------------------------------------------------
add_executable(scene_gen src/sim/main.cpp)
target_link_libraries(scene_gen PRIVATE scitec_sim scitec_frame)
scitect_set_warnings(scene_gen)

# ---------------------------------------------------------------------------
# Sim File Paths
# ---------------------------------------------------------------------------
target_compile_definitions(scene_gen PRIVATE
    SCENE_DATA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Scene.bin"
)
target_compile_definitions(scene_gen PRIVATE
    TRUTH_CSV_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Truth.csv"
)

# ---------------------------------------------------------------------------
# Opir File Paths
# ---------------------------------------------------------------------------
target_compile_definitions(scitec_opir_detection PRIVATE
    SCENE_DATA_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Scene.bin"
)
target_compile_definitions(scitec_opir_detection PRIVATE
    TRUTH_CSV_FILE="${CMAKE_CURRENT_SOURCE_DIR}/Truth.csv"
)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
option(SCITEC_BUILD_TESTS "Build the unit tests" ON)

if(SCITEC_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
