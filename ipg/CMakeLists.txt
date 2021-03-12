# Libraries

if(APPLE)
  add_library(raplcap-ipg raplcap-ipg.c)
  set_target_properties(raplcap-ipg PROPERTIES PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/inc/raplcap.h")
  set(IPG_LINKER_FLAGS "-framework IntelPowerGadget")
  target_link_libraries(raplcap-ipg PRIVATE ${IPG_LINKER_FLAGS})
  if(BUILD_SHARED_LIBS)
    set_target_properties(raplcap-ipg PROPERTIES VERSION ${PROJECT_VERSION}
                                                 SOVERSION ${PROJECT_VERSION_MAJOR})
  endif()
elseif(WIN32)
  # Always build static lib in Windows - can't seem to link against dll with rapl-configure
  add_library(raplcap-ipg STATIC raplcap-ipg.c)
endif()
target_include_directories(raplcap-ipg PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/inc>
                                              $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>)

# pkg-config

set(PKG_CONFIG_EXEC_PREFIX "\${prefix}")
set(PKG_CONFIG_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
set(PKG_CONFIG_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}")
set(PKG_CONFIG_CFLAGS "-I\${includedir}")

set(PKG_CONFIG_NAME "raplcap-ipg")
set(PKG_CONFIG_DESCRIPTION "Implementation of RAPLCap that uses Intel Power Gadget")
set(PKG_CONFIG_REQUIRES_PRIVATE "")
set(PKG_CONFIG_LIBS "-L\${libdir} -lraplcap-ipg")
set(PKG_CONFIG_LIBS_PRIVATE "${IPG_LINKER_FLAGS}")
configure_file(
  ${PROJECT_SOURCE_DIR}/pkgconfig.in
  ${CMAKE_CURRENT_BINARY_DIR}/raplcap-ipg.pc)

# Install

install(TARGETS raplcap-ipg
        EXPORT RAPLCapTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/raplcap-ipg.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
