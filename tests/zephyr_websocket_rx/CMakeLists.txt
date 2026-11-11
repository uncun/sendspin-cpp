# SPDX-License-Identifier: Apache-2.0
cmake_minimum_required(VERSION 3.20.0)
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(sendspin_websocket_rx LANGUAGES C CXX)
target_include_directories(app PRIVATE ../../src/zephyr ${ZEPHYR_BASE}/subsys/net/lib/websocket)
target_sources(app PRIVATE src/main.cpp)
