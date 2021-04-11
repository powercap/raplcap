cmake_minimum_required(VERSION 3.1)

project(raplcap VERSION 0.6.0
                LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_options(-Wall)

include(GNUInstallDirs)

# Tests

enable_testing()

# Subdirectories

# Could compile on any UNIX system, but will only work on Linux
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  add_subdirectory(msr)

  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(POWERCAP powercap>=0.4.0)
    if(POWERCAP_FOUND)
      add_subdirectory(powercap)
    endif()
  endif()
endif()


# Should just be on OSX and Windows
if(APPLE)
  find_library(IntelPowerGadgetLib NAMES "IntelPowerGadget")
  if(IntelPowerGadgetLib)
    add_subdirectory(ipg)
  endif()
elseif(WIN32)
  # No compile-time dependency, so always build
  add_subdirectory(ipg)
endif()

add_subdirectory(rapl-configure)

# Uninstall

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
  @ONLY
)

add_custom_target(uninstall COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
