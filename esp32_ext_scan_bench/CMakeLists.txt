# Banc de test BLE Extended Advertising -- Étage 1 (§6.5 Evolution-XIAO-BLE.md)
cmake_minimum_required(VERSION 3.22)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(MINIMAL_BUILD ON)
project(esp32_ext_scan_bench)
