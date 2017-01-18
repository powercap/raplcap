include_directories(${libmsr_INCLUDE_DIR})

# Libraries

add_library(raplcap-libmsr raplcap-libmsr.c)
target_link_libraries(raplcap-libmsr -L${libmsr_LIB_PATH} msr)

# pkg-config

set(PKG_CONFIG_EXEC_PREFIX "\${prefix}")
set(PKG_CONFIG_LIBDIR "\${prefix}/lib")
set(PKG_CONFIG_INCLUDEDIR "\${prefix}/include/${PROJECT_NAME}")
set(PKG_CONFIG_CFLAGS "-I\${includedir}")

set(PKG_CONFIG_NAME "raplcap-libmsr")
set(PKG_CONFIG_DESCRIPTION "Implementation of RAPLCap that uses LLNL's libmsr")
set(PKG_CONFIG_REQUIRES_PRIVATE "")
set(PKG_CONFIG_LIBS "-L\${libdir} -lraplcap-libmsr")
set(PKG_CONFIG_LIBS_PRIVATE "-L${libmsr_LIB_PATH} -lmsr -lm")
configure_file(
  ${CMAKE_SOURCE_DIR}/pkgconfig.in
  ${CMAKE_BINARY_DIR}/pkgconfig/raplcap-libmsr.pc)

# Install

install(FILES raplcap-libmsr.h DESTINATION include/${PROJECT_NAME})
install(TARGETS raplcap-libmsr DESTINATION "${CMAKE_INSTALL_PREFIX}/lib")
