# Binaries

function(add_rapl_configure RAPL_LIB)
  add_executable(rapl-configure-${RAPL_LIB} rapl-configure.c)
  target_link_libraries(rapl-configure-${RAPL_LIB} PRIVATE raplcap-${RAPL_LIB})
  install(TARGETS rapl-configure-${RAPL_LIB}
          EXPORT RAPLCapTargets
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/rapl-configure.1.in
    ${CMAKE_CURRENT_BINARY_DIR}/man/man1/rapl-configure-${RAPL_LIB}.1
    @ONLY
  )
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/man/man1/rapl-configure-${RAPL_LIB}.1
          DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endfunction()

if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
  add_rapl_configure("msr")
  option(RAPLCAP_CONFIGURE_MSR_EXTRA "Enable extra features in rapl-configure-msr" OFF)
  if(RAPLCAP_CONFIGURE_MSR_EXTRA)
    target_compile_definitions(rapl-configure-msr PRIVATE RAPLCAP_${RAPL_LIB})
    target_include_directories(rapl-configure-msr PRIVATE ../msr)
  endif()
endif()

if(POWERCAP_FOUND)
  add_rapl_configure("powercap")
endif()

if(IntelPowerGadgetLib OR WIN32)
  add_rapl_configure("ipg")
endif()
