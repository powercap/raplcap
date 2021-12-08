# Should just be on OSX and Windows
if(APPLE)
  find_package(IntelPowerGadget 3.0)
  if(NOT IntelPowerGadget_FOUND)
    return()
  endif()
elseif(NOT WIN32)
  return()
endif()

# Libraries

if(APPLE)
  add_raplcap_library(raplcap-ipg ipg IPG SOURCES raplcap-ipg.c)
  set(IPG_LINKER_FLAGS "-framework IntelPowerGadget")
  target_link_libraries(raplcap-ipg PRIVATE IntelPowerGadget::IntelPowerGadget)
elseif(WIN32)
  # Always build static lib in Windows - can't seem to link against dll with rapl-configure
  add_raplcap_library(raplcap-ipg IPG ipg TYPE STATIC
                                          SOURCES raplcap-ipg.c)
endif()
add_raplcap_pkg_config(raplcap-ipg "Implementation of RAPLCap that uses Intel Power Gadget" "" "${IPG_LINKER_FLAGS}" IPG)

# rapl-configure

add_rapl_configure(ipg IPG)
