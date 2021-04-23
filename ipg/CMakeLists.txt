# Should just be on OSX and Windows
if(APPLE)
  find_package(IntelPowerGadget)
  if(NOT IntelPowerGadget_FOUND)
    return()
  endif()
elseif(NOT WIN32)
  return()
endif()

# Libraries

if(APPLE)
  add_library(raplcap-ipg raplcap-ipg.c)
  set(IPG_LINKER_FLAGS "-framework IntelPowerGadget")
  target_link_libraries(raplcap-ipg PRIVATE IntelPowerGadget::IntelPowerGadget)
  if(BUILD_SHARED_LIBS)
    set_target_properties(raplcap-ipg PROPERTIES VERSION ${PROJECT_VERSION}
                                                 SOVERSION ${PROJECT_VERSION_MAJOR})
  endif()
elseif(WIN32)
  # Always build static lib in Windows - can't seem to link against dll with rapl-configure
  add_library(raplcap-ipg STATIC raplcap-ipg.c)
endif()
set_target_properties(raplcap-ipg PROPERTIES PUBLIC_HEADER "${PROJECT_SOURCE_DIR}/inc/raplcap.h")
target_compile_definitions(raplcap-ipg PRIVATE RAPLCAP_IMPL="raplcap-ipg"
                                               RAPLCAP_ALLOW_DEPRECATED)
target_include_directories(raplcap-ipg PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/inc>
                                              $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>)
install(TARGETS raplcap-ipg
        EXPORT RAPLCapTargets
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME})
add_raplcap_pkg_config(raplcap-ipg "Implementation of RAPLCap that uses Intel Power Gadget" "" "${IPG_LINKER_FLAGS}")

# rapl-configure

add_rapl_configure("ipg")
