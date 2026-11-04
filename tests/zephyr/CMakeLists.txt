# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sendspin_zephyr_tests LANGUAGES C CXX)
target_sources(app PRIVATE src/main.cpp src/transport.cpp)
target_include_directories(app PRIVATE ../../src ../../src/zephyr)
target_include_directories(app PRIVATE ${SENDSPIN_NOISE_DIR}/include ${SENDSPIN_ARDUINOJSON_DIR}/src)
