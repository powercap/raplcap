include_directories(${POWERCAP_INCLUDE_DIRS})

# Libraries

add_library(raplcap-powercap raplcap-powercap.c)
target_link_libraries(raplcap-powercap -L${POWERCAP_LIBDIR} ${POWERCAP_LIBRARIES})
if(BUILD_SHARED_LIBS)
  set_target_properties(raplcap-powercap PROPERTIES VERSION ${PROJECT_VERSION}
                                                    SOVERSION ${VERSION_MAJOR})
endif()

# Tests

add_executable(raplcap-powercap-unit-test ${CMAKE_SOURCE_DIR}/test/raplcap-unit-test.c)
target_link_libraries(raplcap-powercap-unit-test raplcap-powercap)
add_test(raplcap-powercap-unit-test raplcap-powercap-unit-test)

# must be run manually
add_executable(raplcap-powercap-integration-test ${CMAKE_SOURCE_DIR}/test/raplcap-integration-test.c)
target_link_libraries(raplcap-powercap-integration-test raplcap-powercap)

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
  ${CMAKE_SOURCE_DIR}/pkgconfig.in
  ${CMAKE_CURRENT_BINARY_DIR}/raplcap-powercap.pc)

# Install

install(TARGETS raplcap-powercap DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/raplcap-powercap.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
