cmake_minimum_required(VERSION 3.16)

# ── test_crc ────────────────────────────────────────────
add_executable(test_crc
    test_crc.c
    ../src/common/crc.c
)
target_include_directories(test_crc PRIVATE ../src)
add_test(NAME test_crc COMMAND test_crc)

# ── test_ring ────────────────────────────────────────────
add_executable(test_ring
    test_ring.c
    ../src/common/ring.c
)
target_include_directories(test_ring PRIVATE ../src)
add_test(NAME test_ring COMMAND test_ring)

# ── test_frame ───────────────────────────────────────────
add_executable(test_frame
    test_frame.c
    ../src/mac/frame.c
    ../src/common/crc.c
)
target_include_directories(test_frame PRIVATE ../src)
add_test(NAME test_frame COMMAND test_frame)

# ── test_sec ─────────────────────────────────────────────
add_executable(test_sec
    test_sec.c
    ../src/sec/sec.c
    ../src/sec/keys.c
    ../src/common/crc.c
)
target_include_directories(test_sec PRIVATE ../src)
if(UMESH_USE_MICROCRYPT)
    target_compile_definitions(test_sec PRIVATE UMESH_USE_MICROCRYPT=1)
    target_link_libraries(test_sec PRIVATE microcrypt)
endif()
add_test(NAME test_sec COMMAND test_sec)

# ── test_posix_loopback ──────────────────────────────────
add_executable(test_posix_loopback
    test_posix_loopback.c
    ../port/posix/phy_posix.c
)
target_include_directories(test_posix_loopback PRIVATE ../src)
add_test(NAME test_posix_loopback COMMAND test_posix_loopback)

# ── test_mac ─────────────────────────────────────────────
add_executable(test_mac
    test_mac.c
    ../src/mac/mac.c
    ../src/mac/cca.c
    ../src/mac/frame.c
    ../src/common/crc.c
    ../src/common/ring.c
    ../src/phy/phy.c
    ../port/posix/phy_posix.c
)
target_include_directories(test_mac PRIVATE ../src ../include ../port/posix)
add_test(NAME test_mac COMMAND test_mac)

# ── test_routing ─────────────────────────────────────────
add_executable(test_routing
    test_routing.c
    ../src/net/routing.c
)
target_include_directories(test_routing PRIVATE ../src)
add_test(NAME test_routing COMMAND test_routing)

# ── test_net ─────────────────────────────────────────────
add_executable(test_net
    test_net.c
    ../src/net/net.c
    ../src/net/routing.c
    ../src/net/discovery.c
    ../src/mac/mac.c
    ../src/mac/cca.c
    ../src/mac/frame.c
    ../src/common/crc.c
    ../src/common/ring.c
    ../src/phy/phy.c
    ../src/sec/sec.c
    ../src/sec/keys.c
    ../port/posix/phy_posix.c
)
target_include_directories(test_net PRIVATE ../src ../include ../port/posix)
if(UMESH_USE_MICROCRYPT)
    target_compile_definitions(test_net PRIVATE UMESH_USE_MICROCRYPT=1)
    target_link_libraries(test_net PRIVATE microcrypt)
endif()
add_test(NAME test_net COMMAND test_net)

# ── test_e2e ─────────────────────────────────────────────
add_executable(test_e2e
    test_e2e.c
    ../src/umesh.c
    ../src/net/net.c
    ../src/net/routing.c
    ../src/net/discovery.c
    ../src/mac/mac.c
    ../src/mac/cca.c
    ../src/mac/frame.c
    ../src/common/crc.c
    ../src/common/ring.c
    ../src/phy/phy.c
    ../src/sec/sec.c
    ../src/sec/keys.c
    ../port/posix/phy_posix.c
)
target_include_directories(test_e2e PRIVATE ../src ../include ../port/posix)
if(UMESH_USE_MICROCRYPT)
    target_compile_definitions(test_e2e PRIVATE UMESH_USE_MICROCRYPT=1)
    target_link_libraries(test_e2e PRIVATE microcrypt)
endif()
add_test(NAME test_e2e COMMAND test_e2e)

add_executable(test_election
    test_election.c
    ../src/net/net.c
    ../src/net/routing.c
    ../src/net/discovery.c
    ../src/mac/mac.c
    ../src/mac/cca.c
    ../src/mac/frame.c
    ../src/common/crc.c
    ../src/common/ring.c
    ../src/phy/phy.c
    ../src/sec/sec.c
    ../src/sec/keys.c
    ../port/posix/phy_posix.c
)
target_include_directories(test_election PRIVATE ../src ../include ../port/posix)
if(UMESH_USE_MICROCRYPT)
    target_compile_definitions(test_election PRIVATE UMESH_USE_MICROCRYPT=1)
    target_link_libraries(test_election PRIVATE microcrypt)
endif()
add_test(NAME test_election COMMAND test_election)

add_executable(test_gradient
    test_gradient.c
    ../src/net/net.c
    ../src/net/routing.c
    ../src/net/discovery.c
    ../src/mac/mac.c
    ../src/mac/cca.c
    ../src/mac/frame.c
    ../src/common/crc.c
    ../src/common/ring.c
    ../src/phy/phy.c
    ../src/sec/sec.c
    ../src/sec/keys.c
    ../port/posix/phy_posix.c
)
target_include_directories(test_gradient PRIVATE ../src ../include ../port/posix)
if(UMESH_USE_MICROCRYPT)
    target_compile_definitions(test_gradient PRIVATE UMESH_USE_MICROCRYPT=1)
    target_link_libraries(test_gradient PRIVATE microcrypt)
endif()
add_test(NAME test_gradient COMMAND test_gradient)
