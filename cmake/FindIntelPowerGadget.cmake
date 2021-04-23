# Find the IntelPowerGadget include file and library.
#
# This module defines the following IMPORTED target(s):
#
# IntelPowerGadget::IntelPowerGadget
# The IntelPowerGadget library, if found.

find_library(IntelPowerGadget_LIBRARY NAMES "IntelPowerGadget")
find_path(
  IntelPowerGadget_INCLUDE_DIR
  NAMES EnergyLib.h
)
mark_as_advanced(
  IntelPowerGadget_LIBRARY
  IntelPowerGadget_INCLUDE_DIR
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  IntelPowerGadget
  REQUIRED_VARS IntelPowerGadget_LIBRARY
                IntelPowerGadget_INCLUDE_DIR
)

if(IntelPowerGadget_FOUND)
  if(NOT TARGET IntelPowerGadget::IntelPowerGadget)
    add_library(IntelPowerGadget::IntelPowerGadget UNKNOWN IMPORTED)
    set_target_properties(IntelPowerGadget::IntelPowerGadget
      PROPERTIES
        IMPORTED_LOCATION "${IntelPowerGadget_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${IntelPowerGadget_INCLUDE_DIR}"
    )
    if(IntelPowerGadget_LIBRARY MATCHES "/([^/]+)\\.framework$")
      set_target_properties(IntelPowerGadget::IntelPowerGadget PROPERTIES
        IMPORTED_LOCATION "${IntelPowerGadget_LIBRARY}/${CMAKE_MATCH_1}"
      )
    endif()
  endif()
endif()
