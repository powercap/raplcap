include_directories(${libmsr_INCLUDE_DIR})

# Libraries

set(MSR_LIB_NAME msr-static)
if(BUILD_SHARED_LIBS)
  set(MSR_LIB_NAME msr)
endif()

# PP0/PP1 support was removed from libmsr after v0.3.1...
include(CheckSymbolExists)
set(CMAKE_REQUIRED_INCLUDES ${CMAKE_REQUIRED_INCLUDES};${libmsr_INCLUDE_DIR})
set(CMAKE_REQUIRED_LIBRARIES ${CMAKE_REQUIRED_LIBRARIES};${MSR_LIB_NAME})
CHECK_SYMBOL_EXISTS(get_pp_rapl_limit msr_rapl.h LIBMSR_PP_SUPPORTED)

add_library(raplcap-libmsr raplcap-libmsr.c)
target_link_libraries(raplcap-libmsr -L${libmsr_LIB_PATH} ${MSR_LIB_NAME})
if(LIBMSR_PP_SUPPORTED)
  target_compile_definitions(raplcap-libmsr PRIVATE LIBMSR_PP_SUPPORTED=1)
endif()
if(BUILD_SHARED_LIBS)
  set_target_properties(raplcap-libmsr PROPERTIES VERSION ${PROJECT_VERSION}
                                                  SOVERSION ${VERSION_MAJOR})
endif()

# pkg-config

set(PKG_CONFIG_EXEC_PREFIX "\${prefix}")
set(PKG_CONFIG_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
set(PKG_CONFIG_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}")
set(PKG_CONFIG_CFLAGS "-I\${includedir}")

set(PKG_CONFIG_NAME "raplcap-libmsr")
set(PKG_CONFIG_DESCRIPTION "Implementation of RAPLCap that uses LLNL's libmsr")
set(PKG_CONFIG_REQUIRES_PRIVATE "")
set(PKG_CONFIG_LIBS "-L\${libdir} -lraplcap-libmsr")
# libmsr creates static and dynamic libs with the same name... "msr" (no "msr-static")
# the linker or user will have to figure out whether to use the static or dynamic lib
set(PKG_CONFIG_LIBS_PRIVATE "-L${libmsr_LIB_PATH} -lmsr -lm")
configure_file(
  ${CMAKE_SOURCE_DIR}/pkgconfig.in
  ${CMAKE_CURRENT_BINARY_DIR}/raplcap-libmsr.pc)

# Install

install(TARGETS raplcap-libmsr DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/raplcap-libmsr.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
install(FILES raplcap-libmsr.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
