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
)
target_link_libraries(scitec_sim_tests PRIVATE scitec_sim GTest::gtest_main)
scitect_set_warnings(scitec_sim_tests)

gtest_discover_tests(scitec_sim_tests)
