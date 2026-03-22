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
