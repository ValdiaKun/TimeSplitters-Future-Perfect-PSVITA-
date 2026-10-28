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

add_executable(tsfp_vita_bootstrap src/main.c src/input_map.c)
target_compile_options(tsfp_vita_bootstrap PRIVATE -Wall -Wextra)
target_link_libraries(tsfp_vita_bootstrap
    vita2d
    SceDisplay_stub SceGxm_stub SceSysmodule_stub SceCtrl_stub
    ScePgf_stub ScePvf_stub SceCommonDialog_stub SceAppMgr_stub
    freetype png jpeg z m
)

vita_create_self(eboot.bin tsfp_vita_bootstrap)
vita_create_vpk(tsfp-vita-bootstrap.vpk ${VITA_TITLEID} eboot.bin
    VERSION ${VITA_VERSION}
    NAME "TSFP Vita Bootstrap"
)
