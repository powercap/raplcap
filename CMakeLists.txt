cmake_minimum_required(VERSION 3.1)

project(raplcap VERSION 0.6.0
                LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_options(-Wall)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Tests

enable_testing()

# Functions

function(add_raplcap_pkg_config TARGET DESCRIPTION REQUIRES_PRIVATE LIBS_PRIVATE)
  set(PKG_CONFIG_PREFIX "${CMAKE_INSTALL_PREFIX}")
  set(PKG_CONFIG_EXEC_PREFIX "\${prefix}")
  set(PKG_CONFIG_LIBDIR "\${prefix}/${CMAKE_INSTALL_LIBDIR}")
  set(PKG_CONFIG_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}")
  set(PKG_CONFIG_NAME "${TARGET}")
  set(PKG_CONFIG_DESCRIPTION "${DESCRIPTION}")
  string(REPLACE ";" " " PKG_CONFIG_REQUIRES_PRIVATE "${REQUIRES_PRIVATE}")
  set(PKG_CONFIG_CFLAGS "-I\${includedir}")
  set(PKG_CONFIG_LIBS "-L\${libdir} -l${TARGET}")
  string(REPLACE ";" " " PKG_CONFIG_LIBS_PRIVATE "${LIBS_PRIVATE}")
  configure_file(
    ${PROJECT_SOURCE_DIR}/pkgconfig.in
    ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.pc
    @ONLY
  )
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.pc DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)
endfunction()

# Subdirectories

add_subdirectory(rapl-configure)
add_subdirectory(msr)
add_subdirectory(powercap)
add_subdirectory(ipg)

# Uninstall

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
  @ONLY
)

add_custom_target(uninstall COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
