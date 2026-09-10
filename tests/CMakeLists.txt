find_package(GTest 1.11 QUIET)

if(NOT GTest_FOUND)
    message(STATUS "GTest not found locally; fetching it from upstream.")
    include(FetchContent)
    FetchContent_Declare(googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

include(GoogleTest)

add_executable(scitec_sim_tests
    sim/SceneSimulatorTest.cpp
    sim/TruthWriterTest.cpp
    sim/TruthTableTest.cpp
)
target_link_libraries(scitec_sim_tests PRIVATE scitec_sim GTest::gtest_main)
target_include_directories(scitec_sim_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
scitect_set_warnings(scitec_sim_tests)

gtest_discover_tests(scitec_sim_tests)

add_executable(scitec_frame_tests
    frame/FrameWriterTest.cpp
    frame/FrameReaderTest.cpp
)
target_link_libraries(scitec_frame_tests PRIVATE scitec_frame GTest::gtest_main)
target_include_directories(scitec_frame_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
scitect_set_warnings(scitec_frame_tests)

gtest_discover_tests(scitec_frame_tests)

add_executable(scitec_detect_tests
    detect/CfarTest.cpp
    detect/ClusterTest.cpp
)
target_link_libraries(scitec_detect_tests PRIVATE scitec_detect GTest::gtest_main)
target_include_directories(scitec_detect_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
scitect_set_warnings(scitec_detect_tests)

gtest_discover_tests(scitec_detect_tests)

add_executable(scitec_filter_tests
    filter/AlphaBetaTest.cpp
    filter/TrackerTest.cpp
)
target_link_libraries(scitec_filter_tests PRIVATE scitec_filter GTest::gtest_main)
target_include_directories(scitec_filter_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
scitect_set_warnings(scitec_filter_tests)

gtest_discover_tests(scitec_filter_tests)
