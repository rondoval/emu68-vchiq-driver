# Hosted CLI tools (-mcrt=nix20, the toolchain default): the freestanding flags of
# the two modules are their own and do not reach this directory.
#
#   VCHIQInfo  the user's check of the link to the VideoCore: the setup in plain
#              words, gencmd, the connection's counters and the shared window
#              (vchiq.device)
#   VCHIQTest  the test rig the suite drives (tests/): one command per run,
#              facts as key=value (STATUS, AUDIO, AHIWALK, AHITONE, DEVICE, ...)
#
# common/ is the tools' shared code: a VCHIQ service as Exec I/O
# (vchiq_client.c), the debug-log writer (debugport.c), the task, signal and
# timer a tool waits with (waiter.c), ReadArgs() defaults (args.h).  The rig's
# output grammar is emu68-common's (Emu68Common::kv, <emu68test/kv.h>).
# The video tools (VCInfo, VCGrab, VCTest) are videocodec-library's.

set(VCHIQ_TOOL_COMPILE_OPTIONS
    -Os
    -Wall
    -Wextra
    -Wconversion
    -Wsign-conversion
    -Wshadow
    -Wmissing-prototypes
    -Wstrict-prototypes
)

add_library(tools_common STATIC
    common/vchiq_client.c
    common/debugport.c
    common/waiter.c
)
target_include_directories(tools_common PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/common)
target_compile_options(tools_common PRIVATE ${VCHIQ_TOOL_COMPILE_OPTIONS})
# drv_timer and the string helpers from emu68-common
target_link_libraries(tools_common PUBLIC vchiq_headers Emu68Common::common Emu68Common::kv)

add_executable(VCHIQInfo
    VCHIQInfo.c
)
# the window layout (vchiq_core.h) for DUMP: read-only, a diagnostic tool's privilege
target_include_directories(VCHIQInfo PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../vchiq/include
)
target_compile_options(VCHIQInfo PRIVATE ${VCHIQ_TOOL_COMPILE_OPTIONS})
target_compile_definitions(VCHIQInfo PRIVATE
    VERSTAG="$VER: VCHIQInfo ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR} ${CURRENT_DATE}"
)
target_link_options(VCHIQInfo PRIVATE -s)
target_link_libraries(VCHIQInfo PRIVATE tools_common)

add_executable(VCHIQTest
    vchiqtest/vchiqtest.c
    vchiqtest/status.c
    vchiqtest/abandon.c
    vchiqtest/audio.c
    vchiqtest/ahi.c
    vchiqtest/device.c
)
# the test-only command's number (vchiq/vchiq_test.h): a test rig's privilege; the AUDS wire format
target_include_directories(VCHIQTest PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../vchiq/include
    ${VCHIQ_SHARED_DIR}/include
)
target_compile_options(VCHIQTest PRIVATE ${VCHIQ_TOOL_COMPILE_OPTIONS})
target_compile_definitions(VCHIQTest PRIVATE
    VERSTAG="$VER: VCHIQTest ${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR} ${CURRENT_DATE}"
)
target_link_options(VCHIQTest PRIVATE -s)
target_link_libraries(VCHIQTest PRIVATE tools_common)

# C: the user's check (build.sh uploads C/).  Developer/ goes into the archive
# only: the test rig (the test suite uploads it to RAM: for its runs).
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/VCHIQInfo DESTINATION C)
install(PROGRAMS ${CMAKE_CURRENT_BINARY_DIR}/VCHIQTest DESTINATION Developer/Tools)
