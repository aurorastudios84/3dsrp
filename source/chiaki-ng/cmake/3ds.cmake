# 3DS (devkitARM / libctru) toolchain file for chiaki-ng.
#
# Usage (inside devkitpro/devkitarm):
#   cmake -B build-3ds -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=cmake/3ds.cmake \
#     -DCHIAKI_ENABLE_CLI=OFF -DCHIAKI_ENABLE_GUI=OFF ...
#
# Modelled on cmake/switch.cmake, the closest existing devkitPro port in this
# tree. It reuses devkitPro's own 3DS.cmake, which pulls in devkitARM and
# points CMAKE_FIND_ROOT_PATH at libctru + portlibs/3ds.
set(DEVKITPRO "$ENV{DEVKITPRO}" CACHE PATH "Path to DevKitPro")
if(NOT DEVKITPRO)
	message(FATAL_ERROR "Please set DEVKITPRO in the environment. https://devkitpro.org/wiki/Getting_Started")
endif()

include("${DEVKITPRO}/cmake/3DS.cmake")

set(PORTLIBS "${DEVKITPRO}/portlibs/3ds" CACHE PATH "Path to devkitpro 3ds portlibs")

# Consumed by the top-level CMakeLists.txt the same way NSWITCH is on Switch.
set(N3DS TRUE)
add_definitions(-D__3DS__)

message(STATUS "3DS build: DEVKITPRO=${DEVKITPRO}")
message(STATUS "3DS build: PORTLIBS=${PORTLIBS}")
message(STATUS "3DS build: C compiler=${CMAKE_C_COMPILER}")
message(STATUS "3DS build: system name=${CMAKE_SYSTEM_NAME} processor=${CMAKE_SYSTEM_PROCESSOR}")
