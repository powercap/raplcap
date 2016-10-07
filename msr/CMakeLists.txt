include_directories(${libmsr_INCLUDE_DIR})

# Libraries

add_library(raplcap-msr raplcap-msr.c)
target_link_libraries(raplcap-msr -L${libmsr_LIB_PATH} msr)

# pkg-config

set(PKG_CONFIG_EXEC_PREFIX "\${prefix}")
set(PKG_CONFIG_LIBDIR "\${prefix}/lib")
set(PKG_CONFIG_INCLUDEDIR "\${prefix}/include")
set(PKG_CONFIG_CFLAGS "-I\${includedir}")

set(PKG_CONFIG_NAME "raplcap-msr")
set(PKG_CONFIG_DESCRIPTION "Implementation of RAPLCap that uses LLNL's libmsr")
set(PKG_CONFIG_REQUIRES_PRIVATE "")
set(PKG_CONFIG_LIBS "-L\${libdir} -lraplcap-msr")
set(PKG_CONFIG_LIBS_PRIVATE "-L${libmsr_LIB_PATH} -lmsr -lm")
configure_file(
  ${CMAKE_SOURCE_DIR}/pkgconfig.in
  ${CMAKE_BINARY_DIR}/pkgconfig/raplcap-msr.pc)

# Install

install(TARGETS raplcap-msr DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
