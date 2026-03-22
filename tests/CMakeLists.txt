cmake_minimum_required(VERSION 3.16)

# ── test_crc ────────────────────────────────────────────
add_executable(test_crc
    test_crc.c
    ../src/common/crc.c
)
target_include_directories(test_crc PRIVATE ../src)
add_test(NAME test_crc COMMAND test_crc)
