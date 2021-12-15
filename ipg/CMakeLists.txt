# Should just be on OSX and Windows
if(APPLE)
  set(IPG_MIN_VERSION 3.0)
  find_package(IntelPowerGadget ${IPG_MIN_VERSION})
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
  raplcap_export_private_dependency(IPG IntelPowerGadget ${IPG_MIN_VERSION})
elseif(WIN32)
  # Always build static lib in Windows - can't seem to link against dll with rapl-configure
  add_raplcap_library(raplcap-ipg ipg IPG TYPE STATIC
                                          SOURCES raplcap-ipg.c)
endif()
install_raplcap_export(IPG)
add_raplcap_pkg_config(raplcap-ipg "Implementation of RAPLCap that uses Intel Power Gadget" "" "${IPG_LINKER_FLAGS}" IPG)

# rapl-configure

add_rapl_configure(ipg IPG)
install_rapl_configure_export(IPG)
