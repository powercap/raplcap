# Could compile on any UNIX system, but will only work on Linux
if(NOT ${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  return()
endif()

set(POWERCAP_MIN_VERSION 0.4.0)
find_package(Powercap ${POWERCAP_MIN_VERSION})
if(NOT Powercap_FOUND)
  return()
endif()

# Libraries

set(SOURCES raplcap-powercap.c powercap-intel-rapl.c)
add_raplcap_library(raplcap-powercap powercap Powercap SOURCES ${SOURCES})
target_link_libraries(raplcap-powercap PRIVATE Powercap::powercap)
raplcap_export_private_dependency(Powercap Powercap ${POWERCAP_MIN_VERSION})
install_raplcap_export(Powercap)
add_raplcap_pkg_config(raplcap-powercap "Implementation of RAPLCap that uses libpowercap (powercap)" "powercap >= ${POWERCAP_MIN_VERSION}" "" Powercap)

# Tests

add_raplcap_tests(raplcap-powercap)

# rapl-configure

add_rapl_configure(powercap Powercap)
install_rapl_configure_export(Powercap)
