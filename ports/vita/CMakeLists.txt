cmake_minimum_required(VERSION 3.16)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
    if(DEFINED ENV{VITASDK})
        set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Vita toolchain")
    else()
        message(FATAL_ERROR "Set VITASDK and put its bin directory on PATH")
    endif()
endif()

project(tsfp_vita_bootstrap C)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(VITA_TITLEID "TFPV00001")
set(VITA_VERSION "01.00")

add_executable(tsfp_vita_bootstrap src/main.c src/input_map.c src/diagnostics.c)
option(TSFP_DIAGNOSTIC_TELEMETRY "Record bounded diagnostic-loop elapsed times (not game FPS)" OFF)
if(TSFP_DIAGNOSTIC_TELEMETRY)
    target_sources(tsfp_vita_bootstrap PRIVATE ../../profiling/telemetry.c)
    target_include_directories(tsfp_vita_bootstrap PRIVATE ../../profiling)
    target_compile_definitions(tsfp_vita_bootstrap PRIVATE TSFP_DIAGNOSTIC_TELEMETRY)
endif()
target_compile_options(tsfp_vita_bootstrap PRIVATE -Wall -Wextra)
target_link_libraries(tsfp_vita_bootstrap
    vita2d
    SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub
    ScePgf_stub ScePvf_stub SceCommonDialog_stub SceAppMgr_stub
    freetype png jpeg z m
)

# VitaSDK macros mutate their flag variables; isolate each SELF invocation.
function(tsfp_create_self output executable)
    vita_create_self(${output} ${executable})
endfunction()

tsfp_create_self(eboot.bin tsfp_vita_bootstrap)
vita_create_vpk(tsfp-vita-bootstrap.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME "TSFP Vita Bootstrap"
)

# Isolated, asset-free native GXM memory/presentation experiment.
# It CPU-paints diagnostic pixels and does NOT execute a shader or Xbox title.
# Opt in to avoid changing the existing Vita diagnostic and guest VPK.
option(TSFP_BUILD_GXM_SMOKE "Build native GXM framebuffer diagnostic" OFF)
if(TSFP_BUILD_GXM_SMOKE)
    add_executable(tsfp_gxm_smoke src/gxm_smoke.c)
    target_compile_options(tsfp_gxm_smoke PRIVATE -O2 -Wall -Wextra -Werror)
    target_link_libraries(tsfp_gxm_smoke SceGxm_stub SceDisplay_stub SceSysmem_stub SceKernelThreadMgr_stub)
    tsfp_create_self(gxm-smoke.bin tsfp_gxm_smoke)
    vita_create_vpk(tsfp-gxm-smoke.vpk "TFPV00003" gxm-smoke.bin
        VERSION ${VITA_VERSION} NAME "TSFP GXM Smoke")

    # Native GPU-executed, shader-driven indexed draw (precompiled vitaGL clear
    # shaders, see src/shaders/). Cross-built only; physical Vita unverified.
    add_executable(tsfp_gxm_draw src/gxm_draw.c src/usse_arena.c)
    target_compile_options(tsfp_gxm_draw PRIVATE -O2 -Wall -Wextra -Werror)
    target_link_libraries(tsfp_gxm_draw SceGxm_stub SceDisplay_stub SceSysmem_stub SceKernelThreadMgr_stub)
    tsfp_create_self(gxm-draw.bin tsfp_gxm_draw)
    vita_create_vpk(tsfp-gxm-draw.vpk "TFPV00004" gxm-draw.bin
        VERSION ${VITA_VERSION} NAME "TSFP GXM Draw")
endif()

# Asset-free guest ABI fixture VPK; deliberately separate from the diagnostic.
option(TSFP_BUILD_GUEST_TESTS "Cross-build generated synthetic guest fixtures" OFF)
if(TSFP_BUILD_GUEST_TESTS)
    get_filename_component(TSFP_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
    if(NOT EXISTS "${TSFP_ROOT}/build/guest/fixtures.c")
        message(FATAL_ERROR "Run python3 tools/generate_fixtures.py from the repository root first")
    endif()
    add_executable(tsfp_guest_test
        "${TSFP_ROOT}/tests/guest_harness.c"
        "${TSFP_ROOT}/guest/memory.c"
        "${TSFP_ROOT}/guest/heap.c"
        "${TSFP_ROOT}/tests/heap_suite.c"
        "${TSFP_ROOT}/guest/execution.c"
        "${TSFP_ROOT}/guest/fault_report.c"
        "${TSFP_ROOT}/profiling/telemetry.c"
        "${TSFP_ROOT}/profiling/guest_run.c"
        "${TSFP_ROOT}/guest/kernel.c"
        "${TSFP_ROOT}/guest/image.c"
        "${TSFP_ROOT}/guest/xbe.c"
        "${TSFP_ROOT}/guest/tls.c"
        "${TSFP_ROOT}/guest/handles.c"
        "${TSFP_ROOT}/guest/worker.c"
        "${TSFP_ROOT}/guest/storage.c"
        "${TSFP_ROOT}/guest/thread_service.c"
        "${TSFP_ROOT}/platform/threads_pthread.c"
        "${TSFP_ROOT}/tests/image_suite.c"
        "${TSFP_ROOT}/tests/kernel_suite.c"
        "${TSFP_ROOT}/tests/wait_suite.c"
        "${TSFP_ROOT}/tests/event_suite.c"
        "${TSFP_ROOT}/tests/worker_suite.c"
        "${TSFP_ROOT}/tests/storage_suite.c"
        "${TSFP_ROOT}/tests/thread_service_suite.c"
        "${TSFP_ROOT}/tests/execution_suite.c"
        "${TSFP_ROOT}/guest/address_space.c"
        "${TSFP_ROOT}/guest/string_ops.c"
        "${TSFP_ROOT}/tests/string_suite.c"
        "${TSFP_ROOT}/tests/atomic_suite.c"
        "${TSFP_ROOT}/build/guest/string_table.c"
        "${TSFP_ROOT}/tests/memory_suite.c"
        "${TSFP_ROOT}/tests/generated_memory_suite.c"
        "${TSFP_ROOT}/tests/mmx_state.c"
        "${TSFP_ROOT}/build/guest/fixtures.c")
    target_include_directories(tsfp_guest_test PRIVATE
        "${TSFP_ROOT}/guest" "${TSFP_ROOT}/profiling" "${TSFP_ROOT}/tests" "${TSFP_ROOT}/build/guest/include")
    target_compile_definitions(tsfp_guest_test PRIVATE TSFP_PORTABLE_GUEST_MEMORY TSFP_REQUIRE_ARMV7)
    target_compile_options(tsfp_guest_test PRIVATE -O2 -Wall -Wextra -Werror
        -Wno-unused-label -Wno-unused-variable -Wno-unused-but-set-variable
        -fno-strict-aliasing -ffp-contract=off -mno-unaligned-access)
    target_link_libraries(tsfp_guest_test pthread m)
    target_link_options(tsfp_guest_test PRIVATE -Wl,--wrap=clock_gettime -Wl,--wrap=pthread_cond_timedwait -Wl,--wrap=__clock_gettime64 -Wl,--wrap=__pthread_cond_timedwait64 -Wl,--wrap=pthread_create -Wl,--wrap=pthread_join -Wl,--wrap=calloc -Wl,--wrap=pthread_cond_wait -Wl,--wrap=tsfp_event_create -Wl,--wrap=tsfp_thread_service_create -Wl,--wrap=tsfp_heap_allocate)
    tsfp_create_self(guest-test.bin tsfp_guest_test)
    vita_create_vpk(tsfp-guest-test.vpk "TFPV00002" guest-test.bin
        VERSION ${VITA_VERSION} NAME "TSFP Guest ABI Tests")
endif()
