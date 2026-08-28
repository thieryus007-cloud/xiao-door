cmake_minimum_required(VERSION 3.20.0)

list(APPEND BOARD_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../vendor/platform-seeedboards/zephyr")

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(xiao_ext_adv_test)

target_sources(app PRIVATE src/main.c)
