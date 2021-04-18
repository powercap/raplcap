# Binaries

function(add_rapl_configure RAPL_LIB)
  add_executable(rapl-configure-${RAPL_LIB} ${PROJECT_SOURCE_DIR}/rapl-configure/rapl-configure.c)
  target_link_libraries(rapl-configure-${RAPL_LIB} PRIVATE raplcap-${RAPL_LIB})
  install(TARGETS rapl-configure-${RAPL_LIB}
          EXPORT RAPLCapTargets
          RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
  configure_file(
    ${PROJECT_SOURCE_DIR}/rapl-configure/rapl-configure.1.in
    ${CMAKE_CURRENT_BINARY_DIR}/man/man1/rapl-configure-${RAPL_LIB}.1
    @ONLY
  )
  install(FILES ${CMAKE_CURRENT_BINARY_DIR}/man/man1/rapl-configure-${RAPL_LIB}.1
          DESTINATION ${CMAKE_INSTALL_MANDIR}/man1)
endfunction()
