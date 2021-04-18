# Libraries

add_library(raplcap-msr raplcap-msr.c
                        raplcap-msr-common.c
                        raplcap-msr-sys-linux.c
                        raplcap-cpuid.c)
target_include_directories(raplcap-msr PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/inc$<SEMICOLON>${CMAKE_CURRENT_SOURCE_DIR}>
                                              $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>)
set_target_properties(raplcap-msr PROPERTIES PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/inc/raplcap.h;raplcap-msr.h")
target_compile_definitions(raplcap-msr PRIVATE RAPLCAP_IMPL="raplcap-msr"
                                               RAPLCAP_ALLOW_DEPRECATED)
if(BUILD_SHARED_LIBS)
  set_target_properties(raplcap-msr PROPERTIES VERSION ${PROJECT_VERSION}
                                               SOVERSION ${PROJECT_VERSION_MAJOR})
endif()
install(TARGETS raplcap-msr
        EXPORT RAPLCapTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
add_raplcap_pkg_config(raplcap-msr "Implementation of RAPLCap that uses the MSR directly" "" "")

# Tests

add_executable(raplcap-msr-unit-test ${PROJECT_SOURCE_DIR}/test/raplcap-unit-test.c)
target_link_libraries(raplcap-msr-unit-test PRIVATE raplcap-msr)
add_test(raplcap-msr-unit-test raplcap-msr-unit-test)

add_executable(raplcap-msr-common-unit-test test/raplcap-msr-common-test.c
                                            raplcap-msr-common.c
                                            raplcap-cpuid.c)
target_include_directories(raplcap-msr-common-unit-test PRIVATE ${PROJECT_SOURCE_DIR}/inc)
add_test(raplcap-msr-common-unit-test raplcap-msr-common-unit-test)

# must be run manually
add_executable(raplcap-msr-integration-test ${PROJECT_SOURCE_DIR}/test/raplcap-integration-test.c)
target_compile_definitions(raplcap-msr-integration-test PRIVATE RAPLCAP_ALLOW_DEPRECATED)
target_link_libraries(raplcap-msr-integration-test PRIVATE raplcap-msr)

# rapl-configure

add_rapl_configure("msr")
option(RAPLCAP_CONFIGURE_MSR_EXTRA "Enable extra features in rapl-configure-msr" OFF)
target_compile_definitions(rapl-configure-msr PRIVATE $<$<BOOL:${RAPLCAP_CONFIGURE_MSR_EXTRA}>:RAPLCAP_msr>)
