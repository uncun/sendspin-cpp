# Copyright 2026 Sendspin Contributors
# SPDX-License-Identifier: Apache-2.0
if(NOT CONFIG_SENDSPIN)
    return()
endif()

if(NOT CONFIG_CSPRNG_ENABLED AND NOT CONFIG_TEST_CSPRNG_GENERATOR)
    message(FATAL_ERROR "Sendspin requires a cryptographic random generator (enable an entropy driver)")
endif()

get_filename_component(SENDSPIN_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)

# Dependencies are fetched by west update, never at CMake configure time. Ask west
# for paths so manifest path overrides and import path-prefix work as expected.
function(sendspin_dependency variable project)
    if(DEFINED ENV{${variable}} AND NOT DEFINED ${variable})
        set(${variable} "$ENV{${variable}}" CACHE PATH "Path to ${project}")
    endif()
    if(NOT DEFINED ${variable})
        set(result 1)
        if(WEST)
            execute_process(COMMAND ${WEST} list ${project} -f "{abspath}"
                WORKING_DIRECTORY ${WEST_TOPDIR}
                OUTPUT_VARIABLE dependency_dir OUTPUT_STRIP_TRAILING_WHITESPACE
                RESULT_VARIABLE result ERROR_QUIET)
        endif()
        if(NOT result EQUAL 0 OR NOT IS_DIRECTORY "${dependency_dir}")
            if(DEFINED WEST_TOPDIR AND IS_DIRECTORY "${WEST_TOPDIR}/modules/lib/${project}")
                set(dependency_dir "${WEST_TOPDIR}/modules/lib/${project}")
            elseif(DEFINED ZEPHYR_BASE AND IS_DIRECTORY "${ZEPHYR_BASE}/../modules/lib/${project}")
                set(dependency_dir "${ZEPHYR_BASE}/../modules/lib/${project}")
            else()
                message(FATAL_ERROR "Missing ${project}: import sendspin-cpp's west.yml and run west update, or set ${variable}")
            endif()
        endif()
        set(${variable} "${dependency_dir}" CACHE PATH "Path to ${project}")
    endif()
endfunction()

sendspin_dependency(SENDSPIN_ARDUINOJSON_DIR arduinojson)
sendspin_dependency(SENDSPIN_NOISE_DIR noise-c)

include(${SENDSPIN_ROOT}/cmake/sources.cmake)
sendspin_get_sources(${SENDSPIN_ROOT})
zephyr_library_named(sendspin)
zephyr_library_compile_options(-UESP_PLATFORM)
zephyr_library_sources(${SENDSPIN_CORE_SOURCES}
    ${SENDSPIN_ROOT}/src/zephyr/client_connection.cpp
    ${SENDSPIN_ROOT}/src/zephyr/network_info.cpp
    ${SENDSPIN_ROOT}/src/zephyr/noise_rand.cpp)
zephyr_library_include_directories(${SENDSPIN_ROOT}/src ${SENDSPIN_ROOT}/src/zephyr
    ${SENDSPIN_NOISE_DIR}/include ${SENDSPIN_ARDUINOJSON_DIR}/src)
zephyr_include_directories(${SENDSPIN_ROOT}/include)
zephyr_library_compile_definitions(SENDSPIN_OUTBOUND_ONLY)
zephyr_compile_definitions(ARDUINOJSON_ENABLE_STD_STRING=1 ARDUINOJSON_USE_LONG_LONG=1)

foreach(role PLAYER CONTROLLER METADATA COLOR ARTWORK VISUALIZER SOURCE)
    if(CONFIG_SENDSPIN_ENABLE_${role})
        zephyr_library_sources(${SENDSPIN_${role}_SOURCES})
        # Roles change public header declarations, so consumers need the same flags.
        zephyr_compile_definitions(SENDSPIN_ENABLE_${role})
    endif()
endforeach()

if(CONFIG_SENDSPIN_ENABLE_PLAYER OR CONFIG_SENDSPIN_ENABLE_SOURCE)
    sendspin_dependency(SENDSPIN_MICRO_FLAC_DIR micro-flac)
    include(${SENDSPIN_MICRO_FLAC_DIR}/cmake/sources.cmake)
    flac_get_sources(${SENDSPIN_MICRO_FLAC_DIR})
    zephyr_library_sources(${FLAC_SOURCES})
    zephyr_library_include_directories(${SENDSPIN_MICRO_FLAC_DIR}/include
        ${SENDSPIN_MICRO_FLAC_DIR}/src)
    zephyr_library_compile_definitions(MICRO_FLAC_DISABLE_OGG MICRO_FLAC_DISABLE_XTENSA_ASM)
endif()

# noise-c only ships an ESP-IDF build. Use its reference ChaChaPoly/25519/SHA256
# backend, with a custom RNG backed by Zephyr's cryptographic random API.
set(noise_sources
    protocol/cipherstate.c protocol/dhstate.c protocol/errors.c
    protocol/handshakestate.c protocol/hashstate.c protocol/internal.c
    protocol/names.c protocol/patterns.c protocol/randstate.c protocol/signstate.c
    protocol/symmetricstate.c protocol/util.c
    backend/ref/cipher-chachapoly.c backend/ref/dh-curve25519.c backend/ref/hash-sha256.c
    crypto/chacha/chacha.c crypto/donna/poly1305-donna.c crypto/sha2/sha256.c
    crypto/x25519/x25519.c)
foreach(source IN LISTS noise_sources)
    zephyr_library_sources(${SENDSPIN_NOISE_DIR}/src/${source})
endforeach()
zephyr_library_include_directories(${SENDSPIN_NOISE_DIR}/src)
zephyr_library_compile_definitions(NOISE_USE_REFERENCE_BACKEND=1 NOISE_USE_AES=0
    NOISE_USE_LIBSODIUM=0 NOISE_USE_CUSTOM_RAND=1)

# The pinned reference backend expands defined() in a macro and passes a single
# limb to mul(..., nb=1), whose array parameter GCC treats as eight limbs. Keep
# these dependency diagnostics from breaking Zephyr's default -Werror test builds.
set_source_files_properties(${SENDSPIN_NOISE_DIR}/src/crypto/donna/poly1305-donna.c
    PROPERTIES COMPILE_OPTIONS "-Wno-expansion-to-defined")
if(CMAKE_C_COMPILER_ID STREQUAL "GNU")
    set_source_files_properties(${SENDSPIN_NOISE_DIR}/src/crypto/x25519/x25519.c
        PROPERTIES COMPILE_OPTIONS "-Wno-stringop-overread")
endif()
