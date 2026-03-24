cmake_minimum_required(VERSION 3.16)
project(umesh C)
set(CMAKE_C_STANDARD 99)

# microcrypt — optional crypto library
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/microcrypt/src/mcrypt.c")
    set(UMESH_USE_MICROCRYPT ON)
    add_library(microcrypt STATIC
        third_party/microcrypt/src/mcrypt.c)
    target_include_directories(microcrypt PUBLIC
        third_party/microcrypt/include)
    message(STATUS "uMesh: microcrypt found and enabled")
else()
    set(UMESH_USE_MICROCRYPT OFF)
    message(STATUS "uMesh: microcrypt not found, using built-in AES")
endif()

set(UMESH_SOURCES
    src/umesh.c
    src/phy/phy.c
    src/mac/mac.c
    src/mac/cca.c
    src/mac/frame.c
    src/net/net.c
    src/net/routing.c
    src/net/discovery.c
    src/power/power.c
    src/sec/sec.c
    src/sec/keys.c
    src/common/crc.c
    src/common/ring.c
)

add_library(umesh STATIC ${UMESH_SOURCES})
target_include_directories(umesh PUBLIC  include)
target_include_directories(umesh PRIVATE src)

if(UMESH_USE_MICROCRYPT)
    target_compile_definitions(umesh PRIVATE UMESH_USE_MICROCRYPT=1)
    target_link_libraries(umesh PRIVATE microcrypt)
endif()

if(UMESH_PORT STREQUAL "esp32")
    target_sources(umesh PRIVATE port/esp32/phy_esp32.c)
    target_sources(umesh PRIVATE port/esp32/power_esp32.c)
else()
    target_sources(umesh PRIVATE port/posix/phy_posix.c)
    target_sources(umesh PRIVATE port/posix/power_posix.c)
    message(STATUS "uMesh: POSIX port (testing)")
endif()

option(UMESH_BUILD_TESTS "Build tests" ON)
if(UMESH_BUILD_TESTS AND NOT UMESH_PORT STREQUAL "esp32")
    enable_testing()
    add_subdirectory(tests)
endif()
