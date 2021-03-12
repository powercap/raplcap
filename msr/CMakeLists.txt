# Libraries

add_library(raplcap-msr raplcap-msr.c
                        raplcap-msr-common.c
                        raplcap-msr-sys-linux.c
                        raplcap-cpuid.c)
target_compile_definitions(raplcap-msr PRIVATE RAPLCAP_IMPL="raplcap-msr")
if(BUILD_SHARED_LIBS)
  set_target_properties(raplcap-msr PROPERTIES VERSION ${PROJECT_VERSION}
                                               SOVERSION ${PROJECT_VERSION_MAJOR})
endif()

# Tests

add_executable(raplcap-msr-unit-test ${PROJECT_SOURCE_DIR}/test/raplcap-unit-test.c)
target_link_libraries(raplcap-msr-unit-test PRIVATE raplcap-msr)
add_test(raplcap-msr-unit-test raplcap-msr-unit-test)

add_executable(raplcap-msr-common-unit-test test/raplcap-msr-common-test.c
                                            raplcap-msr-common.c
                                            raplcap-cpuid.c)
add_test(raplcap-msr-common-unit-test raplcap-msr-common-unit-test)

# must be run manually
add_executable(raplcap-msr-integration-test ${PROJECT_SOURCE_DIR}/test/raplcap-integration-test.c)
target_link_libraries(raplcap-msr-integration-test PRIVATE raplcap-msr)

# pkg-config

set(PKG_CONFIG_EXEC_PREFIX "\${prefix}")
set(PKG_CONFIG_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
set(PKG_CONFIG_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}")
set(PKG_CONFIG_CFLAGS "-I\${includedir}")

set(PKG_CONFIG_NAME "raplcap-msr")
set(PKG_CONFIG_DESCRIPTION "Implementation of RAPLCap that uses the MSR directly")
set(PKG_CONFIG_REQUIRES_PRIVATE "")
set(PKG_CONFIG_LIBS "-L\${libdir} -lraplcap-msr")
set(PKG_CONFIG_LIBS_PRIVATE "")
configure_file(
  ${PROJECT_SOURCE_DIR}/pkgconfig.in
  ${CMAKE_CURRENT_BINARY_DIR}/raplcap-msr.pc)

# Install

install(TARGETS raplcap-msr DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/raplcap-msr.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(FILES raplcap-msr.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})