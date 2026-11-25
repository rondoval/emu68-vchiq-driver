cmake_minimum_required(VERSION 3.14.0)
project(emu68-vchiq-driver VERSION 0.7)
# The one version of the repository: every binary takes it (project(... VERSION
# ${CMAKE_PROJECT_VERSION}) in its directory, the tools directly), the sources have no
# number of their own and the SFDs carry none.  Bump it here only.

# Two binaries and their tools:
#   vchiq/       vchiq.device        the VCHIQ transport to the VPU firmware (DEVS:)
#   ahi/         videocore.audio     an AHI sub-driver on the firmware's audio service
#                                    (DEVS:AHI/, its mode file in DEVS:AudioModes/)
#   tools/       VCHIQInfo (C:); VCHIQTest, the test rig (Developer/Tools)
# The video libraries (videocodec.library, videocore.codec, vcjpeg.datatype) went to
# videocodec-library at 0.7: Kinora builds and ships them.

# vchiq.device's test-only commands (vchiq/vchiq_test.h), for the hardware test
# suite (tests/); never in a release.  The stack forwards
# EMU68_VCHIQ_TEST_HOOKS here.
option(VCHIQ_TEST_HOOKS "vchiq.device: the test-only commands of the test suite (tests/)" OFF)


# Version strings (stack convention, see emu68-gic400-library): "$VER: name x.y (dd.mm.yyyy)"
string(TIMESTAMP CURRENT_DATE "(%d.%m.%Y)")
if(EMU68_BUILD_STAMP)   # local-build marker from the stack's build.sh; empty in releases
    string(APPEND CURRENT_DATE " ${EMU68_BUILD_STAMP}")
endif()

# Dependencies: emu68-common (devtree helpers, driver task, reset guard, cache
# ops, MMIO/timing headers, dma_mem, drv_waitq) and gic400.library's headers (the
# doorbell interrupt server).  The client headers of mailbox.resource (the VCHIQ init
# tag goes through MB_RawCommand) and cma.resource (the window) come with the
# toolchain image (amiga-build-container).
find_package(Emu68Common 2.4 CONFIG REQUIRED) # 2.4: the test harness (Emu68Common::kv, emu68test); 2.3: prefs_open(), strfit(); 2.1: drv_waitq, the dma-ranges/symbol/carve-out devtree helpers
find_package(GIC400 1.3 CONFIG REQUIRED)

if(CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_SOURCE_DIR}/install" CACHE PATH "Install path" FORCE)
endif()

# Debug-output backend defines (EMU68_DEBUG_BACKEND, see emu68-common).
emu68_debug_definitions()
# cache_ops.h LVO-fallback define (EMU68_FORCE_LVO_CACHE_OPS, see emu68-common).
emu68_cache_ops_definitions()

# Warnings and code generation shared by the two freestanding binaries
# (CPU/FPU/frame-pointer flags come from the stack toolchain).
set(VCHIQ_MODULE_COMPILE_OPTIONS
    -O3
    -ffreestanding
    -Wall
    -Wextra
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wmissing-prototypes
    -Wstrict-prototypes
)
set(VCHIQ_MODULE_LINK_OPTIONS
    -nostdlib
    -nostartfiles
    -Wall
    -Wextra
    -s
)

# Sources built into more than one binary: the AUDS wire format (auds_proto.h), for
# videocore.audio and VCHIQTest.
set(VCHIQ_SHARED_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shared)

add_subdirectory(vchiq)
add_subdirectory(ahi)
add_subdirectory(tools)
