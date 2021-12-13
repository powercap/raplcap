# Find the powercap library.
# Doesn't support components, which RAPLCap doesn't need.
# Version 0.4.0 has a cmake package config, but it's not included in the Debian/Ubuntu -dev package until 0.5.0.

include(FindPackageHandleStandardArgs)

# Use installed package config if available
find_package(Powercap ${Powercap_FIND_VERSION} QUIET CONFIG)
if(Powercap_FOUND)
  find_package_handle_standard_args(Powercap CONFIG_MODE)
  return()
endif()

# Fall back on pkg-config search
find_package(PkgConfig QUIET)
if(PKG_CONFIG_FOUND)
  pkg_check_modules(PC_Powercap QUIET powercap)
endif()
find_package_handle_standard_args(Powercap
                                  REQUIRED_VARS PC_Powercap_LINK_LIBRARIES
                                                PC_Powercap_INCLUDE_DIRS
                                  VERSION_VAR PC_Powercap_VERSION)
if(Powercap_FOUND)
  if(NOT TARGET Powercap::powercap)
    add_library(Powercap::powercap UNKNOWN IMPORTED)
    set_target_properties(Powercap::powercap
      PROPERTIES
        IMPORTED_LINK_INTERFACE_LANGUAGES "C"
        IMPORTED_LOCATION "${PC_Powercap_LINK_LIBRARIES}"
        INTERFACE_INCLUDE_DIRECTORIES "${PC_Powercap_INCLUDE_DIRS}"
    )
  endif()
endif()
