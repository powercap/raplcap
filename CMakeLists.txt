cmake_minimum_required(VERSION 2.8.9)

project(raplcap)
set(PROJECT_VERSION 0.1.0)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -std=gnu99")

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

add_subdirectory(msr)

find_package(PkgConfig REQUIRED)
pkg_check_modules(POWERCAP powercap)
if(POWERCAP_FOUND)
  add_subdirectory(powercap)
endif()

find_package(libmsr)
if(libmsr_FOUND)
  add_subdirectory(libmsr)
endif()

add_subdirectory(rapl-configure)
