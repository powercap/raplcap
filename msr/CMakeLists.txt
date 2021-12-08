# Could compile on any UNIX system, but will only work on Linux
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  return()
endif()

# Libraries

set(SOURCES raplcap-msr.c
            raplcap-msr-common.c
            raplcap-msr-sys-linux.c
            raplcap-cpuid.c)
add_raplcap_library(raplcap-msr msr SOURCES ${SOURCES}
                                    PUBLIC_HEADER raplcap-msr.h
                                    PUBLIC_BUILD_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR})
add_raplcap_pkg_config(raplcap-msr "Implementation of RAPLCap that uses the MSR directly" "" "")

# Tests

add_raplcap_tests(raplcap-msr)

add_executable(raplcap-msr-common-unit-test test/raplcap-msr-common-test.c
                                            raplcap-msr-common.c
                                            raplcap-cpuid.c)
target_include_directories(raplcap-msr-common-unit-test PRIVATE ${PROJECT_SOURCE_DIR}/inc)
add_test(raplcap-msr-common-unit-test raplcap-msr-common-unit-test)

# rapl-configure

add_rapl_configure("msr")
option(RAPLCAP_CONFIGURE_MSR_EXTRA "Enable extra features in rapl-configure-msr" OFF)
target_compile_definitions(rapl-configure-msr PRIVATE $<$<BOOL:${RAPLCAP_CONFIGURE_MSR_EXTRA}>:RAPLCAP_msr>)
