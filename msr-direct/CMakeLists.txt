# Libraries

add_library(raplcap-msr-direct raplcap-msr-direct.c)
target_link_libraries(raplcap-msr-direct m)

# pkg-config

set(PKG_CONFIG_EXEC_PREFIX "\${prefix}")
set(PKG_CONFIG_LIBDIR "\${prefix}/lib")
set(PKG_CONFIG_INCLUDEDIR "\${prefix}/include")
set(PKG_CONFIG_CFLAGS "-I\${includedir}")

set(PKG_CONFIG_NAME "raplcap-msr-direct")
set(PKG_CONFIG_DESCRIPTION "Implementation of RAPLCap that uses the MSR directly")
set(PKG_CONFIG_REQUIRES_PRIVATE "")
set(PKG_CONFIG_LIBS "-L\${libdir} -lraplcap-msr-direct")
set(PKG_CONFIG_LIBS_PRIVATE "-lm")
configure_file(
  ${CMAKE_SOURCE_DIR}/pkgconfig.in
  ${CMAKE_BINARY_DIR}/pkgconfig/raplcap-msr-direct.pc)

# Install

install(TARGETS raplcap-msr-direct DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
