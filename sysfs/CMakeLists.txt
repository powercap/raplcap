include_directories(${POWERCAP_INCLUDE_DIRS})

# Libraries

add_library(raplcap-sysfs raplcap-sysfs.c)
target_link_libraries(raplcap-sysfs -L${POWERCAP_LIBDIR} ${POWERCAP_LIBRARIES})

# pkg-config

set(PKG_CONFIG_EXEC_PREFIX "\${prefix}")
set(PKG_CONFIG_LIBDIR "\${prefix}/lib")
set(PKG_CONFIG_INCLUDEDIR "\${prefix}/include")
set(PKG_CONFIG_CFLAGS "-I\${includedir}")

set(PKG_CONFIG_NAME "raplcap-sysfs")
set(PKG_CONFIG_DESCRIPTION "Implementation of RAPLCap that uses libpowercap (sysfs)")
set(PKG_CONFIG_REQUIRES_PRIVATE "powercap")
set(PKG_CONFIG_LIBS "-L\${libdir} -lraplcap-sysfs")
set(PKG_CONFIG_LIBS_PRIVATE "")
configure_file(
  ${CMAKE_SOURCE_DIR}/pkgconfig.in
  ${CMAKE_BINARY_DIR}/pkgconfig/raplcap-sysfs.pc)

# Install

install(TARGETS raplcap-sysfs DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
