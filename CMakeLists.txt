cmake_minimum_required(VERSION 3.12...3.30)

project(raplcap VERSION 0.10.0
                LANGUAGES C)

set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)
add_compile_options(-Wall)

include(GNUInstallDirs)
set(RAPLCAP_CMAKE_CONFIG_INSTALL_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/raplcap)

list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake)

# Tests

enable_testing()

# Shared Header

add_library(raplcap INTERFACE)
target_include_directories(raplcap INTERFACE $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/inc>
                                             $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>)
install(FILES ${PROJECT_SOURCE_DIR}/inc/raplcap.h
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
        COMPONENT RAPLCap_Development)
install(TARGETS raplcap
        EXPORT RAPLCapTargets
        COMPONENT RAPLCap_Development)
install(EXPORT RAPLCapTargets
        DESTINATION ${RAPLCAP_CMAKE_CONFIG_INSTALL_DIR}
        NAMESPACE RAPLCap::
        COMPONENT RAPLCap_Development)

# Functions

function(add_raplcap_library TARGET SHORT_NAME COMP_PART)
  # Parse arguments
  set(options)
  set(oneValueArgs TYPE)
  set(multiValueArgs SOURCES
                     PUBLIC_HEADER
                     PUBLIC_BUILD_INCLUDE_DIRS)
  cmake_parse_arguments(ARG "${options}" "${oneValueArgs}" "${multiValueArgs}" ${ARGN})
  if(NOT "${ARG_UNPARSED_ARGUMENTS}" STREQUAL "")
    message(FATAL_ERROR "add_raplcap_library: unrecognized args: ${ARG_UNPARSED_ARGUMENTS}")
  endif()

  # Create library
  add_library(${TARGET} ${ARG_TYPE} ${ARG_SOURCES})
  target_link_libraries(${TARGET} PUBLIC raplcap)
  target_include_directories(${TARGET} PUBLIC $<BUILD_INTERFACE:${ARG_PUBLIC_BUILD_INCLUDE_DIRS}>
                                              $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}>
                                       PRIVATE ${PROJECT_SOURCE_DIR}/inc)
  set_target_properties(${TARGET} PROPERTIES PUBLIC_HEADER "${ARG_PUBLIC_HEADER}")
  target_compile_definitions(${TARGET} PRIVATE RAPLCAP_IMPL="${TARGET}"
                                               RAPLCAP_ALLOW_DEPRECATED)
  if(BUILD_SHARED_LIBS)
    set_target_properties(${TARGET} PROPERTIES VERSION ${PROJECT_VERSION}
                                               SOVERSION ${PROJECT_VERSION_MAJOR})
  endif()
  install(TARGETS ${TARGET}
          EXPORT RAPLCap${COMP_PART}Targets
          LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
                  COMPONENT RAPLCap_${COMP_PART}_Runtime
                  NAMELINK_COMPONENT RAPLCap_${COMP_PART}_Development
          ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
                  COMPONENT RAPLCap_${COMP_PART}_Development
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
                  COMPONENT RAPLCap_${COMP_PART}_Runtime
          PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}
                        COMPONENT RAPLCap_${COMP_PART}_Development)
endfunction()

function(add_raplcap_pkg_config TARGET DESCRIPTION REQUIRES_PRIVATE LIBS_PRIVATE COMP_PART)
  set(PKG_CONFIG_PREFIX "${CMAKE_INSTALL_PREFIX}")
  set(PKG_CONFIG_EXEC_PREFIX "\${prefix}")
  if(IS_ABSOLUTE "${CMAKE_INSTALL_LIBDIR}")
    set(PKG_CONFIG_LIBDIR "${CMAKE_INSTALL_LIBDIR}")
  else()
    set(PKG_CONFIG_LIBDIR "\${exec_prefix}/${CMAKE_INSTALL_LIBDIR}")
  endif()
  if(IS_ABSOLUTE "${CMAKE_INSTALL_INCLUDEDIR}")
    set(PKG_CONFIG_INCLUDEDIR "${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}")
  else()
    set(PKG_CONFIG_INCLUDEDIR "\${prefix}/${CMAKE_INSTALL_INCLUDEDIR}/${PROJECT_NAME}")
  endif()
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
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${TARGET}.pc
          DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
          COMPONENT RAPLCap_${COMP_PART}_Development)
endfunction()

function(raplcap_export_private_dependency COMP PACKAGE PACKAGE_VERSION)
  if(BUILD_SHARED_LIBS)
    return()
  endif()
  # This approach avoids creating variables/macros in the Config.cmake file which might conflict with other packages.
  # Refer to Config.cmake.in to see how this fits into the component discovery loop for graceful failures.
  set_property(GLOBAL APPEND PROPERTY RAPLCAP_EXPORT_COMPONENT_DEPENDENCIES_PROP "
    if(_comp STREQUAL ${COMP})
      find_package(${PACKAGE} ${PACKAGE_VERSION})
      if(NOT ${PACKAGE}_FOUND)
        set(\${CMAKE_FIND_PACKAGE_NAME}_\${_comp}_FOUND False)
        set(\${CMAKE_FIND_PACKAGE_NAME}_NOT_FOUND_MESSAGE \"Component \${_comp} missing dependency: ${PACKAGE}\")
      endif()
    endif()"
  )
endfunction()

function(install_raplcap_export COMP_PART)
  install(EXPORT RAPLCap${COMP_PART}Targets
          DESTINATION ${RAPLCAP_CMAKE_CONFIG_INSTALL_DIR}
          NAMESPACE RAPLCap::
          COMPONENT RAPLCap_${COMP_PART}_Development)
endfunction()

# Subdirectories

add_subdirectory(rapl-configure)
add_subdirectory(test)

add_subdirectory(msr)
add_subdirectory(powercap)

# CMake package helper

include(CMakePackageConfigHelpers)

set(CONFIG_TARGETS_FILE RAPLCapTargets.cmake)
set(CONFIG_SUPPORTED_COMPONENTS MSR MSRUtils Powercap PowercapUtils)
get_property(RAPLCAP_EXPORT_COMPONENT_DEPENDENCIES_VAR GLOBAL PROPERTY RAPLCAP_EXPORT_COMPONENT_DEPENDENCIES_PROP)
string(REPLACE ";" "\n" CONFIG_FIND_COMPONENT_DEPENDENCIES "${RAPLCAP_EXPORT_COMPONENT_DEPENDENCIES_VAR}")
configure_package_config_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/Config.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/RAPLCapConfig.cmake
  INSTALL_DESTINATION ${RAPLCAP_CMAKE_CONFIG_INSTALL_DIR}
  NO_SET_AND_CHECK_MACRO
  NO_CHECK_REQUIRED_COMPONENTS_MACRO
)
write_basic_package_version_file(
  ${CMAKE_CURRENT_BINARY_DIR}/RAPLCapConfigVersion.cmake
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/RAPLCapConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/RAPLCapConfigVersion.cmake
        DESTINATION ${RAPLCAP_CMAKE_CONFIG_INSTALL_DIR}
        COMPONENT RAPLCap_Development)
if(NOT BUILD_SHARED_LIBS)
  install(DIRECTORY ${PROJECT_SOURCE_DIR}/cmake/
          DESTINATION ${RAPLCAP_CMAKE_CONFIG_INSTALL_DIR}/Modules
          FILES_MATCHING PATTERN "Find*.cmake")
endif()

# Uninstall

configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/cmake_uninstall.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
  @ONLY
)

add_custom_target(uninstall COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake)
