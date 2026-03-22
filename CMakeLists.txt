cmake_minimum_required(VERSION 3.16)
project(umesh C)
set(CMAKE_C_STANDARD 99)

set(UMESH_SOURCES
    src/umesh.c
    src/phy/phy.c
    src/mac/mac.c
    src/mac/cca.c
    src/mac/frame.c
    src/net/net.c
    src/net/routing.c
    src/net/discovery.c
    src/sec/sec.c
    src/sec/keys.c
    src/common/crc.c
    src/common/ring.c
)

add_library(umesh STATIC ${UMESH_SOURCES})
target_include_directories(umesh PUBLIC  include)
target_include_directories(umesh PRIVATE src)

if(UMESH_PORT STREQUAL "esp32")
    target_sources(umesh PRIVATE port/esp32/phy_esp32.c)
else()
    target_sources(umesh PRIVATE port/posix/phy_posix.c)
    message(STATUS "uMesh: POSIX port (testovanie)")
endif()

option(UMESH_BUILD_TESTS "Build tests" ON)
if(UMESH_BUILD_TESTS AND NOT UMESH_PORT STREQUAL "esp32")
    enable_testing()
    add_subdirectory(tests)
endif()
