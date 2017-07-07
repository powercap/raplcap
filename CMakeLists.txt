cmake_minimum_required(VERSION 2.8)

project(raplcap)
set(PROJECT_VERSION 0.1.0)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=c99")

include_directories(${PROJECT_SOURCE_DIR}/inc)


# Install

install(FILES inc/raplcap.h DESTINATION include/${PROJECT_NAME})
install(DIRECTORY ${CMAKE_BINARY_DIR}/pkgconfig/ DESTINATION lib/pkgconfig)

# Uninstall

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
  @ONLY
)

add_custom_target(uninstall COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)

# Subdirectories

# Could compile on any UNIX system, but will only work on Linux
if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  add_subdirectory(msr)

  find_package(PkgConfig)
  if(PKG_CONFIG_FOUND)
    pkg_check_modules(POWERCAP powercap)
    if(POWERCAP_FOUND)
      add_subdirectory(powercap)
    endif()
  endif()

  find_package(libmsr)
  if(libmsr_FOUND)
    add_subdirectory(libmsr)
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
