cmake_minimum_required (VERSION 3.14)
project (lru_cache)

set(CMAKE_CXX_STANDARD 14)

SET(CMAKE_BUILD_TYPE "Debug")
SET(CMAKE_CXX_FLAGS_DEBUG "$ENV{CXXFLAGS} -O0 -Wall -g2 -ggdb")
SET(CMAKE_CXX_FLAGS_RELEASE "$ENV{CXXFLAGS} -O3 -Wall")

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/03597a01ee50ed33e9dfd640b249b4be3799d395.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(
    lru_test
    test.cc
)
target_link_libraries(
  lru_test
  GTest::gtest_main
)
include(GoogleTest)
gtest_discover_tests(lru_test)