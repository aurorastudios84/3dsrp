cmake_minimum_required(VERSION 3.22)
project(relay LANGUAGES C CXX)
get_filename_component(ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../../../.." ABSOLUTE)
set(CORE "${ROOT}/relay/vendor/chiaki-upstream")
set(DEPS "${ROOT}/source/chiaki-ng")
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(ENABLE_PROGRAMS OFF CACHE BOOL "" FORCE)
set(USE_SHARED_MBEDTLS_LIBRARY OFF CACHE BOOL "" FORCE)
add_subdirectory("${ROOT}/relay/vendor/mbedtls" "${CMAKE_CURRENT_BINARY_DIR}/mbedtls" EXCLUDE_FROM_ALL)
file(GLOB GF "${DEPS}/third-party/gf-complete/src/*.c")
list(FILTER GF EXCLUDE REGEX "gf_method.c|gf_unit.c")
# Reuse the existing FEC dependency sources; the streaming core is pinned upstream.
set(GF_FILES gf.c gf_wgen.c gf_w4.c gf_w8.c gf_w16.c gf_w32.c gf_w64.c gf_w128.c gf_rand.c gf_general.c gf_cpu.c)
set(GF)
foreach(f ${GF_FILES})
 list(APPEND GF "${DEPS}/third-party/gf-complete/src/${f}")
endforeach()
add_library(gf_complete STATIC ${GF})
target_include_directories(gf_complete PUBLIC "${DEPS}/third-party/gf-complete/include")
file(GLOB JER "${DEPS}/third-party/jerasure/src/*.c")
add_library(jerasure STATIC ${JER})
target_include_directories(jerasure PUBLIC "${DEPS}/third-party/jerasure/include")
target_link_libraries(jerasure PUBLIC gf_complete)
set(PB "${DEPS}/third-party/nanopb")
add_library(nanopb STATIC "${PB}/pb_common.c" "${PB}/pb_encode.c" "${PB}/pb_decode.c")
target_include_directories(nanopb PUBLIC "${PB}")
set(FILES common sock session base64 http ctrl rpcrypt takion senkusha streamconnection ecdh launchspec random gkcrypt audio audioreceiver audiosender videoreceiver frameprocessor packetstats discovery congestioncontrol stoppipe reorderqueue discoveryservice feedback feedbacksender controller takionsendbuffer time fec regist orientation bitstream log thread)
set(SOURCES)
foreach(f ${FILES})
 list(APPEND SOURCES "${CORE}/lib/src/${f}.c")
endforeach()
add_library(chiaki STATIC ${SOURCES} "${CMAKE_CURRENT_LIST_DIR}/remote_disabled.c" generated/takion.pb.c)
target_include_directories(chiaki PUBLIC "${CORE}/lib/include" "${CMAKE_CURRENT_LIST_DIR}/generated" "${ROOT}/relay/common")
target_compile_definitions(chiaki PUBLIC CHIAKI_LIB_ENABLE_MBEDTLS)
target_compile_options(chiaki PRIVATE -O2 -Wno-unused-variable)
target_link_libraries(chiaki PUBLIC mbedtls mbedx509 mbedcrypto nanopb jerasure)
add_library(relay SHARED bridge.cpp)
target_link_libraries(relay PRIVATE chiaki android log)
# Keep the test APK usable on both 4KiB and modern 16KiB-page devices.
target_link_options(relay PRIVATE "-Wl,-z,max-page-size=16384")
# Debug APK keeps symbols, but the per-picture native scaler must be optimized.
target_compile_options(relay PRIVATE -O3)

# Native JPEG preserves odd image heights; upstream requires a separate build.
include(ExternalProject)
set(JPEG_BUILD "${CMAKE_CURRENT_BINARY_DIR}/jpeg")
set(JPEG_SIMD ON)
if(ANDROID_ABI STREQUAL "x86_64")
 set(JPEG_SIMD OFF)
endif()
ExternalProject_Add(relay_jpeg_build
 SOURCE_DIR "${ROOT}/relay/vendor/libjpeg-turbo-3.1.3"
 BINARY_DIR "${JPEG_BUILD}"
 CMAKE_ARGS -DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}
  -DANDROID_ABI=${ANDROID_ABI} -DANDROID_PLATFORM=android-26
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_POSITION_INDEPENDENT_CODE=ON
  -DENABLE_SHARED=OFF -DENABLE_STATIC=ON -DWITH_TURBOJPEG=OFF
  -DWITH_TOOLS=OFF -DWITH_TESTS=OFF -DWITH_SIMD=${JPEG_SIMD}
 BUILD_COMMAND ${CMAKE_COMMAND} --build <BINARY_DIR> --target jpeg-static -j2
 INSTALL_COMMAND ""
 BUILD_BYPRODUCTS "${JPEG_BUILD}/libjpeg.a")
add_library(relay_jpeg STATIC IMPORTED)
set_target_properties(relay_jpeg PROPERTIES IMPORTED_LOCATION "${JPEG_BUILD}/libjpeg.a")
add_dependencies(relay_jpeg relay_jpeg_build)
add_dependencies(relay relay_jpeg_build)
target_include_directories(relay PRIVATE "${ROOT}/relay/vendor/libjpeg-turbo-3.1.3/src" "${JPEG_BUILD}")
target_link_libraries(relay PRIVATE relay_jpeg)
