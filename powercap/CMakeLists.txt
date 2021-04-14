# Libraries

add_library(raplcap-powercap raplcap-powercap.c powercap-intel-rapl.c)
target_include_directories(raplcap-powercap PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/inc>
                                                   $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>)
set_target_properties(raplcap-powercap PROPERTIES PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/inc/raplcap.h")
target_compile_definitions(raplcap-powercap PRIVATE RAPLCAP_IMPL="raplcap-powercap")
target_link_libraries(raplcap-powercap PRIVATE Powercap::powercap)
if(BUILD_SHARED_LIBS)
  set_target_properties(raplcap-powercap PROPERTIES VERSION ${PROJECT_VERSION}
                                                    SOVERSION ${PROJECT_VERSION_MAJOR})
endif()

# Tests

add_executable(raplcap-powercap-unit-test ${PROJECT_SOURCE_DIR}/test/raplcap-unit-test.c)
target_link_libraries(raplcap-powercap-unit-test PRIVATE raplcap-powercap)
add_test(raplcap-powercap-unit-test raplcap-powercap-unit-test)

# must be run manually
add_executable(raplcap-powercap-integration-test ${PROJECT_SOURCE_DIR}/test/raplcap-integration-test.c)
target_link_libraries(raplcap-powercap-integration-test PRIVATE raplcap-powercap)

# pkg-config

set(PKG_CONFIG_EXEC_PREFIX "\${prefix}")
set(PKG_CONFIG_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
set(PKG_CONFIG_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}")
set(PKG_CONFIG_CFLAGS "-I\${includedir}")

set(PKG_CONFIG_NAME "raplcap-powercap")
set(PKG_CONFIG_DESCRIPTION "Implementation of RAPLCap that uses libpowercap (powercap)")
set(PKG_CONFIG_REQUIRES_PRIVATE "powercap")
set(PKG_CONFIG_LIBS "-L\${libdir} -lraplcap-powercap")
set(PKG_CONFIG_LIBS_PRIVATE "")
configure_file(
  ${PROJECT_SOURCE_DIR}/pkgconfig.in
  ${CMAKE_CURRENT_BINARY_DIR}/raplcap-powercap.pc)

# Install

install(TARGETS raplcap-powercap
        EXPORT RAPLCapTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/raplcap-powercap.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
